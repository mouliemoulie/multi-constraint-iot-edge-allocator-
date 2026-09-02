#include "edge/resource_monitor.h"
#include "common/logger.h"

ResourceMonitor::ResourceMonitor()
    : total_node_failures_(0U),
      total_node_recoveries_(0U) {
}

void ResourceMonitor::registerEdgeNode(std::shared_ptr<EdgeNode> node) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    edge_nodes_[node->getEdgeId()] = node;
}

void ResourceMonitor::unregisterEdgeNode(const std::string& edge_id) {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    edge_nodes_.erase(edge_id);
}

std::shared_ptr<EdgeNode> ResourceMonitor::getEdgeNode(const std::string& edge_id) const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    const auto it = edge_nodes_.find(edge_id);
    if (it != edge_nodes_.end()) {
        return it->second;
    }
    return nullptr;
}

bool ResourceMonitor::isNodeHealthy(const std::shared_ptr<EdgeNode>& node) const {
    // A node is considered healthy if it is marked available. Additional
    // heuristics (e.g. queue-depth-based degraded-health detection) can
    // be layered on here without changing the public interface.
    return (node != nullptr) && node->isAvailable();
}

void ResourceMonitor::checkEdgeNodeHealth() {
    std::lock_guard<std::mutex> lock(nodes_mutex_);

    for (const auto& entry : edge_nodes_) {
        const std::shared_ptr<EdgeNode>& node = entry.second;
        const bool healthy = isNodeHealthy(node);

        if (status_callback_) {
            status_callback_(entry.first, healthy);
        }
    }
}

void ResourceMonitor::updateResourceMetrics() {
    std::lock_guard<std::mutex> lock(nodes_mutex_);

    for (const auto& entry : edge_nodes_) {
        if (entry.second != nullptr && entry.second->isAvailable()) {
            entry.second->simulateResourceFluctuation();
        }
    }
}

void ResourceMonitor::simulateNodeFailure(const std::string& edge_id) {
    std::shared_ptr<EdgeNode> node;
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        const auto it = edge_nodes_.find(edge_id);
        if (it == edge_nodes_.end()) {
            return;
        }
        node = it->second;
        node->setAvailable(false);
        ++total_node_failures_;
    }

    Logger::getInstance().warn("ResourceMonitor",
        "Edge node " + edge_id + " marked as FAILED.");

    // Spec §10: "Resource Monitor -> E03 unavailable -> Resource
    // Controller -> Find tasks assigned to E03 -> Reallocation".
    if (status_callback_) {
        status_callback_(edge_id, false);
    }
}

void ResourceMonitor::recoverNodeFromFailure(const std::string& edge_id) {
    std::shared_ptr<EdgeNode> node;
    {
        std::lock_guard<std::mutex> lock(nodes_mutex_);
        const auto it = edge_nodes_.find(edge_id);
        if (it == edge_nodes_.end()) {
            return;
        }
        node = it->second;
        node->setAvailable(true);
        ++total_node_recoveries_;
    }

    Logger::getInstance().info("ResourceMonitor",
        "Edge node " + edge_id + " RECOVERED and is available again.");

    if (status_callback_) {
        status_callback_(edge_id, true);
    }
}

void ResourceMonitor::registerStatusCallback(NodeStatusCallback callback) {
    status_callback_ = std::move(callback);
}

size_t ResourceMonitor::getAvailableNodeCount() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    size_t count = 0U;
    for (const auto& entry : edge_nodes_) {
        if (entry.second != nullptr && entry.second->isAvailable()) {
            ++count;
        }
    }
    return count;
}

std::vector<std::string> ResourceMonitor::getAvailableNodeIds() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::vector<std::string> result;
    for (const auto& entry : edge_nodes_) {
        if (entry.second != nullptr && entry.second->isAvailable()) {
            result.push_back(entry.first);
        }
    }
    return result;
}

std::vector<std::string> ResourceMonitor::getFailedNodeIds() const {
    std::lock_guard<std::mutex> lock(nodes_mutex_);
    std::vector<std::string> result;
    for (const auto& entry : edge_nodes_) {
        if (entry.second != nullptr && !entry.second->isAvailable()) {
            result.push_back(entry.first);
        }
    }
    return result;
}
