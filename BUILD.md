# Build Instructions

## Dependencies

| Dependency | Debian/Ubuntu package | macOS (Homebrew) | Required? |
|---|---|---|---|
| CMake >= 3.16 | `cmake` | `cmake` | Yes |
| C++17 compiler (GCC 9+/Clang 10+) | `g++` or `clang` | Xcode CLT | Yes |
| SQLite3 dev headers | `libsqlite3-dev` | `sqlite3` | Yes |
| nlohmann/json >= 3.9 | `nlohmann-json3-dev` | `nlohmann-json` | Yes (or vendor — see `third_party/json/README.md`) |
| Eclipse Paho MQTT C++ | `libpaho-mqttpp3-dev` + `libpaho-mqtt3-dev` | `mqtt-cpp` (or build from source) | Optional — build proceeds with MQTT disabled if missing |
| GoogleTest | `libgtest-dev` | `googletest` | Optional — FetchContent will download it if missing |
| Valgrind | `valgrind` | `valgrind` (Linux only upstream; consider `--tool=leaks` on macOS instead) | Optional, for `valgrind_memcheck`/`valgrind_helgrind` targets |
| lcov + genhtml | `lcov` | `lcov` | Optional, for the `coverage` target (or use `gcovr` instead) |
| gcovr | `gcovr` (or `pip install gcovr`) | `gcovr` | Optional fallback if lcov isn't installed |

### One-line dependency install (Ubuntu 22.04+/Debian 12+)

```bash
sudo apt-get update && sudo apt-get install -y \
    cmake g++ libsqlite3-dev nlohmann-json3-dev \
    libpaho-mqttpp3-dev libpaho-mqtt3-dev \
    libgtest-dev valgrind lcov gcovr
```

If your distribution's package repos don't carry Paho (common on older
Ubuntu), build it from source:

```bash
# Paho MQTT C library (dependency of the C++ client)
git clone https://github.com/eclipse/paho.mqtt.c.git
cmake -B paho.mqtt.c/build -S paho.mqtt.c -DPAHO_WITH_SSL=ON -DPAHO_BUILD_STATIC=ON
cmake --build paho.mqtt.c/build --target install

# Paho MQTT C++ client
git clone https://github.com/eclipse/paho.mqtt.cpp.git
cmake -B paho.mqtt.cpp/build -S paho.mqtt.cpp -DPAHO_WITH_SSL=ON
cmake --build paho.mqtt.cpp/build --target install
```

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

Useful CMake options:

| Option | Default | Purpose |
|---|---|---|
| `-DWARNINGS_AS_ERRORS=ON` | `OFF` | Fail the build on any compiler warning (use in CI) |
| `-DENABLE_ASAN=ON` | `OFF` | Build with AddressSanitizer + UBSan |
| `-DENABLE_TSAN=ON` | `OFF` | Build with ThreadSanitizer (mutually exclusive with ASan) |
| `-DENABLE_COVERAGE=ON` | `OFF` | Instrument with `--coverage` for the `coverage` target (use a separate build dir) |
| `-DBUILD_TESTING=OFF` | `ON` | Skip building the GoogleTest suite |

## Running

```bash
./build/iot_simulator --devices 100 --edges 10 --tasks 10000 --strategy multi_constraint
```

The simulator applies bounded back-pressure when all nodes are temporarily busy, so resource-allocation warnings do not flood the console simply because task generation is faster than execution. Truly impossible requests are still recorded as allocation failures.

Run the full spec §18 comparison across all four strategies on one
workload:

```bash
./build/iot_simulator --devices 100 --edges 10 --tasks 10000 --compare
```

See `./build/iot_simulator --help` for all options.

## Running tests

```bash
cd build
ctest --output-on-failure
```

Or run the test binary directly for more granular filtering:

```bash
./build/tests/edge_iot_tests --gtest_filter=ResourceAllocatorTest.*
```

## Running Valgrind

```bash
cmake --build build --target valgrind_memcheck
cat build/valgrind_memcheck_report.txt
```

```bash
cmake --build build --target valgrind_helgrind
cat build/valgrind_helgrind_report.txt
```

Both targets exit non-zero (`--error-exitcode=1`) if Valgrind finds a
real error, so they can be wired directly into CI as a pass/fail gate.
Known third-party false positives (libstdc++ static init, SQLite's
memory pool, pthread TLS setup) are pre-suppressed in
`cmake/valgrind.supp` — if Valgrind flags anything NOT covered by that
file, treat it as a real bug in this project's code.

## Code coverage

Requires a separate build configured with `-DENABLE_COVERAGE=ON` (adds
`--coverage` instrumentation, so keep it out of your normal Debug
build directory):

```bash
sudo apt-get install lcov   # or: sudo apt-get install gcovr

cmake -B build-coverage -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build build-coverage -j$(nproc)
cmake --build build-coverage --target coverage

xdg-open build-coverage/coverage_report/index.html   # Linux
open build-coverage/coverage_report/index.html       # macOS
```

The `coverage` target runs the full test suite, then generates a
per-file HTML report scoped to `src/`/`include/` (test files, fetched
GoogleTest sources, and `third_party/` are excluded from the report —
see `cmake/coverage.cmake` if you need to adjust the filters). Prefers
`lcov`+`genhtml` for a richer report; falls back to `gcovr` if only
that's installed.

## Running with sanitizers (faster alternative to Valgrind during development)

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build-asan -j$(nproc)
./build-asan/iot_simulator --devices 20 --edges 5 --tasks 500
```

## Static analysis (MISRA-adjacent checks)

The project does not bundle a commercial MISRA checker (PC-lint Plus,
Parasoft, etc. are proprietary). If you have `cppcheck` installed, its
MISRA addon can give a useful — though not exhaustive — signal:

```bash
cppcheck --enable=all --addon=misra --std=c++17 -I include src/ 2>&1 | tee misra_report.txt
```

Cross-reference any findings against `docs/MISRA_DEVIATIONS.md` before
filing them as bugs — several are expected and already documented there.

## Verbose task lifecycle trace

Run the simulator with `--verbose` to enable DEBUG logging. The trace shows:

1. Task generation (`TaskGeneration`)
2. Resource allocation strategy and selected edge (`ResourceAllocator`)
3. Scheduling algorithm and selected task on each edge (`TaskScheduler`)
4. Task execution and deadline result (`TaskExecutor`)

Example:

```bash
./build/iot_simulator --devices 10 --edges 3 --tasks 20 --verbose
```

The default scheduling algorithm is **Priority Based**. The allocation strategy is controlled by `--strategy` and defaults to **Multi-Constraint**.
