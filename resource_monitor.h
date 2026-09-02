#ifndef RESOURCE_MONITOR_H
#define RESOURCE_MONITOR_H

#include "edge_node.h"
#include <memory>
#include <map>
#include <mutex>
#include <functional>

/**
 * Monitors the status and resources of all edge nodes
 * Detects failures and reports resource availability
 */
class ResourceMonitor {
public:
    using NodeStatusCallback = std::function<void(const std::string&, bool)>;  // edge_id, is_available

    ResourceMonitor();

    // Register edge nodes
    void registerEdgeNode(std::shared_ptr<EdgeNode> node);
    void unregisterEdgeNode(const std::string& edge_id);

    // Get node information
    std::shared_ptr<EdgeNode> getEdgeNode(const std::string& edge_id) const;
    const std::map<std::string, std::shared_ptr<EdgeNode>>& getAllEdgeNodes() const {
        return edge_nodes_;
    }

    // Monitor operations
    void checkEdgeNodeHealth();  // Simulate health checks
    void updateResourceMetrics();
    void simulateNodeFailure(const std::string& edge_id);
    void recoverNodeFromFailure(const std::string& edge_id);

    // Status callbacks (for resource controller)
    void registerStatusCallback(NodeStatusCallback callback);

    // Query methods
    size_t getAvailableNodeCount() const;
    std::vector<std::string> getAvailableNodeIds() const;
    std::vector<std::string> getFailedNodeIds() const;

    // Statistics
    uint64_t getTotalNodeFailures() const { return total_node_failures_; }
    uint64_t getTotalNodeRecoveries() const { return total_node_recoveries_; }

private:
    std::map<std::string, std::shared_ptr<EdgeNode>> edge_nodes_;
    mutable std::mutex nodes_mutex_;
    NodeStatusCallback status_callback_;
    uint64_t total_node_failures_;
    uint64_t total_node_recoveries_;

    bool isNodeHealthy(const std::shared_ptr<EdgeNode>& node) const;
};

#endif // RESOURCE_MONITOR_H
