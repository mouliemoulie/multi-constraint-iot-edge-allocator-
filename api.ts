export type Configuration = {
  automatic: boolean
  requested_allocation_strategy: string
  requested_scheduling_algorithm: string
  allocation_strategy: string
  allocation_strategy_label: string
  scheduling_algorithm: string
  scheduling_algorithm_label: string
}

export type Status = {
  backend_available: boolean
  running: boolean
  processing_complete: boolean
  devices: number
  edge_nodes: number
  tasks_configured: number
  tasks_generated: number
  active_tasks: number
  available_edge_nodes: number
  configuration: Configuration
}

export type EdgeNode = {
  edge_id: string
  status: string
  cpu_capacity: number
  cpu_utilization_percent: number
  ram_capacity_mb: number
  ram_utilization_percent: number
  bandwidth_capacity_mbps: number
  bandwidth_utilization_percent: number
  latency_ms: number
  active_tasks: number
  queue_length: number
  load: number
}

export type Task = {
  task_id: string
  device_id: string
  sensor_type: string
  cpu_percent: number
  ram_mb: number
  bandwidth_mbps: number
  deadline_ms: number
  priority: number
  status: number
  created_at: string
  edge_id: string
  allocation_strategy: string
  executed_at: string
}

export type ActiveTask = Task & { elapsed_ms?: number; remaining_ms?: number; assigned_edge_id?: string }

export type Metrics = {
  average_latency_ms: number
  average_waiting_time_ms: number
  deadline_miss_rate_percent: number
  average_cpu_utilization_percent: number
  load_imbalance_stddev: number
  throughput_tasks_per_second: number
  task_success_rate_percent: number
  tasks_generated: number
  tasks_completed: number
  tasks_failed: number
}
