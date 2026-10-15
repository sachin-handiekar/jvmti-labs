# JVMTI Labs

This repository contains hands-on labs for learning JVMTI (JVM Tool Interface). They are the companion code for the book [Learning JVMTI](https://github.com/sachin-handiekar/learning-jvmti): lab *N* goes with chapter *N*.

Each lab directory contains a self-contained JVMTI agent with a companion Java test application, CMake build file, and lab-specific README.

## Repository Structure

| Lab | Directory                         | Focus                                                      |
|-----|------------------------------------|------------------------------------------------------------|
| 1   | lab01_capability_checker            | List potential JVMTI capabilities of the JVM               |
| 2   | lab02_basic_agent                   | Agent lifecycle, options parsing, event registration       |
| 3   | lab03_event_registration            | Capabilities, event callbacks, method/thread tracking      |
| 4   | lab04_thread_class_inspection       | Thread dump: states (incl. TIMED_WAITING vs WAITING), stack walking, line numbers |
| 4b  | lab04b_virtual_threads              | Virtual threads (JDK 21+): what GetAllThreads and ThreadStart miss, and how to see them |
| 5   | lab05_heap_stack_agent              | Heap walking with IterateThroughHeap, stack analysis       |
| 6   | lab06_class_transform_agent         | Class load monitor built on ClassFileLoadHook              |
| 6b  | lab06b_class_patcher                | Bytecode rewriting: patch a class at load time, or retransform it in a running JVM |
| 7   | lab07_jvm_runtime_agent             | GC events with GetTime, system properties, object sizing   |
| 8   | lab08_exception_agent               | Exception/ExceptionCatch callbacks, filtering, stack traces |
| 8b  | lab08b_mini_debugger                | A mini debugger: deferred breakpoints, locals, single-stepping, field watchpoints |
| 9   | lab09_advanced_techniques           | Raw monitors, TLS, reentrancy guards, thread-safe logging  |
| 10  | lab10_profiler_agent                | Minimal sampling profiler with flamegraph output           |
| 11  | lab11_deployment_agent              | Production agent: OnLoad + OnAttach, config, log rotation  |
| 12  | lab12_stability_agent               | Security & stability: capability degradation, safe shutdown, signal chaining |
| 13  | lab13_allocation_tracker            | Case study: allocation sampling (SampledObjectAlloc) with JSON output |

### Shared Infrastructure

| Directory | Purpose                                                          |
|-----------|------------------------------------------------------------------|
| common/   | `jvmti_utils.h` -- shared macros (`CHECK_JVMTI_ERROR`), logging |
| cmake/    | `JvmtiCommon.cmake` -- CMake module for JNI/JVMTI setup         |

## Build Instructions

All labs use CMake for cross-platform builds. CI builds every lab with warnings treated as errors and runs it
against its test app on **Linux (GCC)**, **macOS (Clang)** and **Windows (MSVC)** — JDK 17, 21 and 25 on
Linux, JDK 21 on macOS and Windows (lab 04b, virtual threads, needs JDK 21+). Prerequisites:

- A C/C++ compiler (GCC, Clang, or MSVC)
- CMake 3.10+
- A JDK with JVMTI headers (`JAVA_HOME` must be set)

For each lab:

```sh
cd labXX_lab_name
mkdir build && cd build
cmake ..
cmake --build .
```

## Running the Agents

Each agent is loaded using the `-agentpath` JVM argument:

```sh
# Compile the companion Java app
javac TestApp.java

# Run with the agent
java -agentpath:./build/libagent_name.so TestApp
```

- On **Linux** the built library is `libagent_name.so`
- On **macOS** it is `libagent_name.dylib`
- On **Windows** it is `agent_name.dll` (no `lib` prefix)
- Always provide the **absolute path** to the built agent library

See the `README.md` in each lab directory for specific instructions.

## Agent Capabilities Summary

- **Capability Checker** -- prints a representative set of the JVM's potential capabilities
- **Basic Agent** -- demonstrates the complete agent lifecycle (`Agent_OnLoad`/`Agent_OnUnload`) and `key=value` option parsing
- **Event Registration** -- the three-step event model: capabilities, callbacks, notification modes
- **Thread/Class Inspection** -- Java-style thread dump with states, stack frames, and source lines
- **Virtual Threads** -- `VirtualThreadStart`/`End`, and why `GetAllThreads` never returns virtual threads (JDK 21+)
- **Heap/Stack Analysis** -- walks the heap with the modern `IterateThroughHeap` API, dumps thread stacks
- **Class Load Monitor** -- sees every class's bytecode via ClassFileLoadHook, with JDK filtering and a load summary
- **Class Patcher** -- rewrites a string literal in a class's constant pool, at load time or via `RetransformClasses` after attach
- **JVM Runtime** -- times GC events with `GetTime`, reads system properties
- **Exception Handling** -- captures exceptions with throw/catch locations, JDK filtering, and safe JNI use
- **Mini Debugger** -- `SetBreakpoint` on a source line, `GetLocal*`, per-thread `SingleStep`, and `SetFieldModificationWatch`
- **Advanced Techniques** -- raw monitors, Thread-Local Storage, reentrancy guards
- **Profiler** -- sampling profiler: dedicated agent thread, `GetStackTrace`, collapsed-stack/flamegraph output
- **Deployment** -- configurable production agent with `Agent_OnAttach`, rotation-safe logging
- **Stability** -- capability degradation, safe shutdown, chained crash handlers
- **Allocation Tracker** -- full case study sampling all allocation paths via `SampledObjectAlloc` (JDK 11+)

## References

- [Official JVMTI Documentation](https://docs.oracle.com/en/java/javase/21/docs/specs/jvmti.html)

## License

These labs are provided under the MIT License. See [LICENSE](LICENSE) for details.
