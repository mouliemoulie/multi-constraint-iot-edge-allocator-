# MISRA C++ Deviation Record

## Why this document exists

MISRA C++:2008 / C++:2023 were written for safety-critical embedded and
automotive control software (braking systems, engine control units) where
the cost of a single undefined-behavior bug is measured in human safety.
This project is a **user-space, non-safety-critical network/IoT simulator**
that explicitly requires the C++ Standard Library, multithreading, and
third-party libraries (MQTT client, SQLite, JSON) per its own specification.

Full MISRA compliance and "use STL containers, threads, exceptions, and
JSON libraries" are mutually exclusive requirements. Several MISRA rules
exist specifically to **forbid or tightly restrict** the very facilities
this project's functional spec calls for. Rather than silently ignore the
request, or silently ignore the spec, this document records every point of
tension explicitly, the rule number, why the deviation is necessary, and
what compensating discipline is applied instead.

Everything **not** listed below is treated as a hard MISRA rule and is
followed without exception (see "Rules followed without deviation").

---

## Deviations taken (rule, rationale, compensating control)

| # | MISRA Rule (2008 dir. no.) | Restriction | Deviation taken | Compensating control |
|---|---|---|---|---|
| D1 | Rule 18-4-1 | No dynamic heap memory allocation after initialization | `std::shared_ptr`, `std::vector`, `std::queue`, `std::map` used throughout | All allocation goes through smart pointers / STL containers only — **zero** raw `new`/`delete`. RAII guarantees deterministic release. Valgrind target (see `cmake/valgrind.cmake`) verifies no leaks. |
| D2 | Rule 15-0-2 / 15-3-x | Restricted or no exception handling | `std::exception`-derived types used for unrecoverable construction errors (e.g. SQLite open failure) | Exceptions are used **only** at initialization boundaries (constructors, `main`), never across steady-state task-processing hot paths, and never for ordinary control flow. All steady-state error paths use return codes / `AllocationResult`-style structs instead. |
| D3 | Rule 16-0-3 / Table-driven rule against STL | Avoid STL in safety-critical control loops | `std::vector`, `std::queue`, `std::priority_queue`, `std::map`, `std::shared_ptr` used project-wide | Project specification (see uploaded `Project_Structure` doc, §19 "Technologies") explicitly requires STL usage; this is an academic capstone, not a safety-critical control loop. Containers are used behind narrow, single-purpose wrapper classes (`TaskManager`, `EdgeNode`) so any future migration to fixed-capacity containers touches one file each. |
| D4 | Rule 27-0-1 / streams restriction | Avoid `<iostream>`/stream-based I/O | `std::cout`/`std::cerr` used for simulation logging | Logging is diagnostic-only, off the timing-critical allocation path, and gated through a single `Logger` helper (see `src/common/logger.h`) so it can be swapped for a MISRA-compliant sink later without touching call sites. |
| D5 | Rule 5-2-12 (no implicit array-to-pointer decay) / general pointer-arithmetic restrictions | Avoid raw pointers | Third-party C APIs (`sqlite3*`, Paho MQTT C client) return raw pointers | All raw pointers from C APIs are captured **immediately** into an owning wrapper (`DatabaseManager`, `MqttClient`) with a custom deleter and never leave that wrapper's translation unit as raw pointers. |
| D6 | Rule 7-3-x (avoid global namespace pollution) / Singleton discouraged | Global mutable state | `TaskProfile::getInstance()`, `DatabaseManager::getInstance()` are singletons | Scope is limited to two configuration-style objects that are conceptually process-wide (task profile config, DB handle). Both are thread-safe (internal mutex) and constructed via function-local `static` (C++11 guaranteed thread-safe init), avoiding static-init-order fiasco. |
| D7 | Rule 0-1-x on unbounded recursion / dynamic-size loops | Loop bounds must be statically provable | Simulation loops run for a *configurable* number of ticks/tasks (e.g., "10,000 generated tasks") | This is inherent to a discrete-event simulator whose whole purpose is to run a configurable workload. All loop bounds are read once from configuration at startup (not mutated mid-loop by unrelated code), and every loop has an explicit, single exit condition — satisfying the *spirit* (provable termination) even though the bound is not a compile-time constant. |
| D8 | Rule 10-3-1 discourages virtual dispatch in some profiles | Runtime polymorphism (`IoTDevice`, `EdgeNode` hierarchies) | Virtual functions used for `IoTDevice::generateSensorData()` sensor variants | Required by spec §20 ("OOP: Inheritance, Polymorphism"). Virtual dispatch is confined to object-construction-time-fixed hierarchies (a sensor's type never changes after construction), so the dynamic-dispatch cost/predictability concern MISRA targets (control-loop determinism) does not apply here. |

## Rules followed without deviation (non-exhaustive highlights)

These are treated as hard requirements throughout the codebase:

- **No implicit narrowing conversions** (Rule 5-0-3, 5-0-4): all numeric
  conversions are explicit (`static_cast<...>`).
- **No `goto`** (Rule 6-3-1 / 6-6-1... style rules): zero `goto` statements
  in the codebase.
- **Every `if`/`else if` chain has a final `else`** where a value must be
  produced (Rule 6-4-2).
- **Every `switch` has a `default` case** (Rule 6-4-3), including on
  `enum class` switches, even though the compiler can prove exhaustiveness,
  to guard against future enumerator additions.
- **No unnamed namespaces mixed with named ones** in the same file.
- **Single point of exit is preferred** in non-trivial functions; early
  returns are used only as guard clauses at the top of a function.
- **All class data members are private**; access is via accessor
  functions only (Rule 11-0-1).
- **No C-style casts** — only `static_cast`, `dynamic_cast`,
  `const_cast`, or (only in the two low-level C-API wrapper files)
  `reinterpret_cast`, always commented.
- **No macros for constants or functions** (Rule 16-0-4/16-2-x) —
  `constexpr` / `const` used exclusively. Include guards are the *only*
  permitted use of the preprocessor.
- **Every function has a single, well-defined responsibility** and stays
  under ~60 lines where practical.
- **No magic numbers** — all thresholds (queue depth limits, default
  weights, retry counts) are named `constexpr` values declared once.
- **Signed/unsigned mixing is avoided**; array/container indices use
  `std::size_t` consistently.

## What to check first if a static-analysis tool disagrees

If you run a MISRA checker (PC-lint Plus, Cppcheck `--addon=misra`,
Parasoft C++test, etc.) against this codebase, expect findings clustered
around **D1–D8 above** — every one of them is a deliberate, documented
choice, not an oversight. Everything else flagged should be treated as a
genuine defect and fixed; please file it against the module in question.
