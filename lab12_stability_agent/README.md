# Lab 12 — Security and Stability

> **Book:** companion to [Chapter 12: Security and Stability](https://github.com/sachin-handiekar/learning-jvmti/blob/master/manuscript/chapter12.md) of *Learning JVMTI*. Read the chapter for the concepts; build and run the code here.

A JVMTI agent runs as **native code inside the JVM process** — a single null pointer
dereference crashes the entire application. This example demonstrates the defensive
patterns from Chapter 12 that separate prototype agents from production-ready ones.

## What It Demonstrates

| Pattern | Where |
|---------|-------|
| **Defensive capability check** — `GetPotentialCapabilities` + graceful degradation when a feature is unavailable | `Agent_OnLoad` |
| **Safe shutdown** — `shutting_down` flag, disable events *before* cleanup, and *don't* destroy raw monitors that in-flight callbacks may still enter | `cbVMDeath` |
| **Crash-handler chaining** — save and silently delegate to the JVM's SIGSEGV handler, keeping its flags and mask (POSIX) / chain to the previous unhandled-exception filter (Windows) | `install_crash_handlers`, called from `VMInit` |

> **Why chaining matters:** HotSpot *deliberately* triggers SIGSEGV during normal
> operation — implicit null checks in JIT-compiled code and safepoint polling both rely
> on it. An agent that replaces the JVM's handler (e.g., logs and `_exit()`s) will kill a
> perfectly healthy JVM almost immediately. Handlers are installed from `VMInit` (after
> the JVM has installed its own) so the saved "previous" handler is the JVM's. In
> production, prefer `libjsig`:
>
> ```sh
> LD_PRELOAD=$JAVA_HOME/lib/libjsig.so java -agentpath:./build/libstability_agent.so App
> ```

## Build

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Run

```sh
javac StabilityTestApp.java
java -agentpath:./build/libstability_agent.so StabilityTestApp
```

(On Windows use `build\stability_agent.dll`, on macOS `build/libstability_agent.dylib`.)

## Expected Output

```
[Agent] stability_agent loaded (Lab 12)
StabilityTestApp started
StabilityTestApp handled 10 exceptions
StabilityTestApp finished
[Stability] VMDeath — 10 exceptions observed.
[Stability] SIGSEGVs chained to the JVM's handler: N
```

- The exception count can exceed 10 on some JDK versions, which throw and catch exceptions
  internally during startup (Lab 8 covers filtering).
- The SIGSEGV line appears on Linux/macOS only. `N` is often non-zero on a perfectly
  healthy run: each one is a fault the JVM deliberately triggered and recovered from. That
  is exactly why the handler must chain silently instead of logging or exiting.
