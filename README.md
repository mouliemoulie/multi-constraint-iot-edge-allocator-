# Adaptive Multi-Constraint Resource Allocation and Load Balancing for Edge-Based IoT Networks

A C++17 simulation of an IoT edge-computing network: simulated sensor
devices generate data, a task manager converts that data into
computational tasks, and a resource allocator distributes those tasks
across simulated edge nodes using one of four selectable algorithms
(Round Robin, Least Load, Priority-Based, or a weighted Multi-Constraint
scorer). See the original project specification in
`docs/PROJECT_SPEC.md` for the full design rationale — this
implementation follows it section-by-section, referenced throughout the
source comments as `spec §N`.

## Quick start

```bash
cmake -B build && cmake --build build -j$(nproc)
./build/iot_simulator --devices 100 --edges 10 --tasks 10000 --compare
```

See **[docs/BUILD.md](docs/BUILD.md)** for full dependency and build
instructions, and **[docs/MISRA_DEVIATIONS.md](docs/MISRA_DEVIATIONS.md)**
for the project's documented C++ coding-standard deviations.

## Architecture

```
IoT Devices (5 sensor types)
        │  generateSensorData()
        ▼
   TaskManager  ──creates──▶  Task (with TaskRequirements from TaskProfile)
        │
        ▼
ResourceController ──uses──▶ ResourceAllocator (4 strategies)
        │                           │
        │                    scores against
        ▼                           ▼
ResourceMonitor ──tracks──▶  EdgeNode(s) (CPU/RAM/BW/latency/queue)
        │
   (on failure)
        ▼
  Reallocation ──▶ TaskScheduler (4 strategies) ──▶ TaskExecutor
                                                          │
                                                          ▼
                                                  DatabaseManager (SQLite)
                                                          │
                                                          ▼
                                                  MetricsCollector
```

## Module map

| Module | Header | Responsibility |
|---|---|---|
| IoT Device | `include/iot_device/` | Simulated sensors (ultrasonic, temperature, camera, humidity, pressure) |
| MQTT | `include/mqtt/` | Thin transport wrapper (Paho C++); optional at build time |
| Task | `include/task/` | `Task`, `TaskProfile` (config), `TaskManager` (creation/queueing) |
| Edge | `include/edge/` | `EdgeNode` (resource accounting), `ResourceMonitor` (health/failure) |
| Resource | `include/resource/` | `ResourceAllocator` (the 4 algorithms), `ResourceController` (orchestration) |
| Scheduler | `include/scheduler/` | `TaskScheduler` (FIFO / Priority / EDF / Round-robin-slice) |
| Database | `include/database/` | `DatabaseManager` (SQLite persistence, 7 tables per spec §12) |
| Executor | `include/executor/` | `TaskExecutor` (simulated execution, deadline checking) |
| Metrics | `include/metrics/` | `MetricsCollector` (the 7 metrics from spec §17, comparison table from §18) |
| Common | `include/common/` | `Logger`, named constants (no magic numbers project-wide) |


### Allocation back-pressure

The simulator starts edge workers before task generation. Resource reservations are
released when a task finishes, and transient resource exhaustion is handled with
bounded back-pressure rather than immediately marking the task as permanently
failed. A task is enqueued exactly once by the scheduler after allocation.

## Testing

```bash
cd build && ctest --output-on-failure
```

63 unit tests across 6 test files cover the allocator's four strategies,
edge-node resource accounting, task scheduling orderings, and metrics
aggregation. See `tests/`.

## Code quality tooling

- **Compiler warnings**: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`
  and more, always on; `-DWARNINGS_AS_ERRORS=ON` promotes them to errors.
- **Sanitizers**: `-DENABLE_ASAN=ON` or `-DENABLE_TSAN=ON` for fast
  local memory/thread-safety checking.
- **Code coverage**: `-DENABLE_COVERAGE=ON` + `make coverage` generates
  an HTML report scoped to `src/`/`include/` (see `docs/BUILD.md`).
- **Valgrind**: `cmake --build build --target valgrind_memcheck` (see
  `docs/BUILD.md`).
- **MISRA C++ deviations**: fully documented in
  `docs/MISRA_DEVIATIONS.md` — read this before running any MISRA
  static-analysis tool against the codebase.

## License

No license specified — treat as all-rights-reserved unless your
instructor/institution specifies otherwise for coursework submission.

## Web dashboard

The project now includes a real web dashboard that connects to the existing C++
core through a small HTTP API adapter. The allocator, scheduler, executor,
resource monitor, metrics collector, and SQLite database remain the source of
truth.

Start the C++ API/runtime:

```bash
cmake -B build -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
./build/iot_simulator --web --devices 100 --edges 10 --tasks 10000 \
  --strategy multi_constraint --scheduling edf
```

Then start the frontend:

```bash
cd frontend
npm install
npm run dev
```

See `docs/WEB_DASHBOARD.md` for the API contract and configuration flow.
