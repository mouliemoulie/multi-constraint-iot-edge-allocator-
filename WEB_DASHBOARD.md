# Edge IoT Manager Web Dashboard

The web dashboard is an interface to the existing C++ core. The C++ allocator,
scheduler, executor, resource monitor, metrics collector, and SQLite database
remain the source of truth.

## Architecture

```text
React + TypeScript + Vite
          |
          | HTTP/JSON
          v
C++ HTTP API adapter
          |
          +--> LiveRuntime
                  |
                  +--> ResourceAllocator
                  +--> TaskScheduler
                  +--> TaskExecutor
                  +--> ResourceMonitor / EdgeNode
                  +--> DatabaseManager -> SQLite
```

The API adapter uses a small POSIX HTTP server and does not introduce a large
web framework into the C++ core.

## Build the C++ backend

```bash
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
```

For the normal test build, install GoogleTest or use the project's existing
FetchContent path. The web build itself does not require GoogleTest.

## Start the backend

```bash
./build/iot_simulator --web --devices 100 --edges 10 --tasks 10000 \
  --strategy multi_constraint --scheduling edf --verbose
```

The HTTP API listens on `http://localhost:8080` by default. Change the port
with `--web-port 8081` if needed.

The process keeps the API available after the configured workload has finished,
so the dashboard can continue to inspect the final state. The existing command
line mode remains available and is not replaced.

## Start the frontend

```bash
cd frontend
npm install
npm run dev
```

Open the Vite address shown by the terminal, normally `http://localhost:5173`.
The Vite development proxy forwards `/api` calls to the C++ backend on port
8080.

## Configuration flow

Manual mode:

```text
Dashboard dropdowns
      |
      | PUT /api/configuration
      v
LiveRuntime::applyConfiguration()
      |
      +--> ResourceAllocator::setStrategy()
      |
      +--> TaskScheduler::setStrategy()
```

The dropdown values are runtime values. The frontend never edits or generates
`.cpp` files.

Automatic mode sends `{"automatic":true}`. The C++ runtime recommends a
strategy from the current edge-node resource state and applies it to the real
allocator and scheduler.

## API

| Method | Endpoint | Purpose |
|---|---|---|
| GET | `/api/health` | Backend health |
| GET | `/api/status` | Overall runtime status |
| GET | `/api/configuration` | Current requested and effective configuration |
| PUT | `/api/configuration` | Apply allocation/scheduling configuration |
| GET | `/api/devices` | Device fleet |
| GET | `/api/edge-nodes` | Live edge-node resources |
| GET | `/api/tasks` | Persisted task records |
| GET | `/api/tasks/active` | Currently executing tasks |
| GET | `/api/tasks/{task_id}` | One task lifecycle view |
| GET | `/api/allocations` | Allocation decisions and scores |
| GET | `/api/metrics` | MetricsCollector values |
| GET | `/api/activities` | Recent allocation/execution events |
| GET | `/api/alerts` | Resource alerts |
| GET | `/api/resource-history` | Resource history samples |

## Verification

1. Start the backend with `--strategy multi_constraint --scheduling edf`.
2. Open the dashboard and confirm Configuration shows Multi-Constraint / EDF.
3. Disable Automatic / Recommended.
4. Select `Least Load` and `Priority Based`.
5. Click **Apply Configuration**.
6. The dashboard sends a `PUT /api/configuration` request.
7. `ResourceAllocator` receives `LEAST_LOAD` and `TaskScheduler` receives
   `PRIORITY_BASED` at runtime.
8. New allocations are stored with the strategy used in
   `task_allocations.strategy`.
9. The task execution trace and API status expose the effective scheduling
   algorithm.
10. Repeat with `Multi-Constraint` + `EDF` and compare the allocation and
    execution records in the same SQLite database.

## Database compatibility

The existing SQLite database is reused. A small backward-compatible migration
adds the `strategy` column to `task_allocations` when an older database is
opened. No second frontend database is created.
