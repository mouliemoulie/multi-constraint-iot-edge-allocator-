#include "scheduler/task_scheduler.h"
#include "common/logger.h"

#include <algorithm>
#include <deque>

namespace {

const char* schedulingStrategyName(TaskScheduler::SchedulingStrategy strategy) {
    switch (strategy) {
        case TaskScheduler::SchedulingStrategy::FIFO:
            return "FIFO";
        case TaskScheduler::SchedulingStrategy::PRIORITY_BASED:
            return "PRIORITY_BASED";
        case TaskScheduler::SchedulingStrategy::EARLIEST_DEADLINE_FIRST:
            return "EARLIEST_DEADLINE_FIRST";
        case TaskScheduler::SchedulingStrategy::ROUND_ROBIN_TIME_SLICE:
            return "ROUND_ROBIN_TIME_SLICE";
        default:
            return "UNKNOWN";
    }
}

}  // namespace

// ------------------------------------------------------------
// This implementation intentionally reorders EdgeNode's underlying
// std::queue into priority order at "getNextTask" time rather than
// maintaining a separate std::priority_queue per edge. This keeps
// EdgeNode's public interface (a plain FIFO queue, matching spec §7's
// description of edge nodes) decoupled from scheduling policy, which
// belongs entirely to this class (spec §13: "Don't confuse resource
// allocation with task scheduling").
// ------------------------------------------------------------

void TaskScheduler::setStrategy(SchedulingStrategy strategy) {
    std::lock_guard<std::mutex> lock(strategy_mutex_);
    strategy_ = strategy;
}

TaskScheduler::SchedulingStrategy TaskScheduler::getStrategy() const {
    std::lock_guard<std::mutex> lock(strategy_mutex_);
    return strategy_;
}

TaskScheduler::TaskScheduler(SchedulingStrategy strategy)
    : strategy_(strategy),
      total_tasks_scheduled_(0U) {
}

bool TaskScheduler::FifoComparator::operator()(
    const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) const {
    return a->getCreationTime() > b->getCreationTime();  // Earlier created = higher priority
}

bool TaskScheduler::PriorityComparator::operator()(
    const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) const {
    // Spec §13 example: T103(CRITICAL) -> T101(HIGH) -> T104(MEDIUM) -> T102(LOW)
    return a->getRequirements().priority < b->getRequirements().priority;
}

bool TaskScheduler::EdfComparator::operator()(
    const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) const {
    // Earliest Deadline First: task with the LEAST remaining time runs first.
    return a->getRemainingTimeMs() > b->getRemainingTimeMs();
}

void TaskScheduler::scheduleTask(std::shared_ptr<EdgeNode> edge, std::shared_ptr<Task> task) {
    std::lock_guard<std::mutex> strategy_lock(strategy_mutex_);
    if (edge == nullptr || task == nullptr) {
        return;
    }
    task->setStatus(Task::TaskStatus::QUEUED);
    edge->enqueueTask(task);
    ++total_tasks_scheduled_;
    Logger::getInstance().debug(
        "TaskScheduler",
        "Queued task " + task->getTaskId() + " on " + edge->getEdgeId() +
        "; scheduling algorithm=" + schedulingStrategyName(strategy_));
}

std::shared_ptr<Task> TaskScheduler::getNextTask(std::shared_ptr<EdgeNode> edge) {
    std::lock_guard<std::mutex> strategy_lock(strategy_mutex_);
    if (edge == nullptr || !edge->hasTasksInQueue()) {
        return nullptr;
    }

    if (strategy_ == SchedulingStrategy::FIFO) {
        // FIFO matches EdgeNode's underlying queue order directly —
        // no reordering needed.
        return edge->dequeueTask();
    }

    // For PRIORITY_BASED, EARLIEST_DEADLINE_FIRST, and
    // ROUND_ROBIN_TIME_SLICE, drain the edge's queue into a scratch
    // buffer, select according to policy, then restore the remainder.
    std::deque<std::shared_ptr<Task>> scratch;
    while (edge->hasTasksInQueue()) {
        scratch.push_back(edge->dequeueTask());
    }

    if (scratch.empty()) {
        return nullptr;
    }

    std::shared_ptr<Task> selected = nullptr;

    switch (strategy_) {
        case SchedulingStrategy::PRIORITY_BASED: {
            auto it = std::max_element(scratch.begin(), scratch.end(),
                [](const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) {
                    return a->getRequirements().priority < b->getRequirements().priority;
                });
            selected = *it;
            scratch.erase(it);
            break;
        }
        case SchedulingStrategy::EARLIEST_DEADLINE_FIRST: {
            auto it = std::min_element(scratch.begin(), scratch.end(),
                [](const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) {
                    return a->getRemainingTimeMs() < b->getRemainingTimeMs();
                });
            selected = *it;
            scratch.erase(it);
            break;
        }
        case SchedulingStrategy::ROUND_ROBIN_TIME_SLICE: {
            // Simplest round-robin: always take the front of the
            // remaining queue (equivalent to FIFO for single-pass
            // dequeue; time-slice preemption would be layered on at
            // the executor level for a full time-sliced scheduler).
            selected = scratch.front();
            scratch.pop_front();
            break;
        }
        default: {
            selected = scratch.front();
            scratch.pop_front();
            break;
        }
    }

    if (selected != nullptr) {
        Logger::getInstance().debug(
            "TaskScheduler",
            "Selected task " + selected->getTaskId() + " from " + edge->getEdgeId() +
            " using " + schedulingStrategyName(strategy_));
    }

    // Restore remaining tasks back onto the edge's queue in their
    // original relative order.
    for (const auto& remaining_task : scratch) {
        edge->enqueueTask(remaining_task);
    }

    return selected;
}
