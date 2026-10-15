# Lab 09 — Advanced Techniques: Raw Monitors, TLS, Reentrancy

> **Book:** companion to [Chapter 9: Advanced JVMTI Techniques](https://github.com/sachin-handiekar/learning-jvmti/blob/master/manuscript/chapter9.md) of *Learning JVMTI*. Read the chapter for the concepts; build and run the code here.

A thread-safe method counter (§9.6) that counts every method entry on every thread —
exactly, without taking a lock on each call:

- **Thread-Local Storage** (`SetThreadLocalStorage` / `GetThreadLocalStorage`): each thread
  counts in its own struct, so `MethodEntry` — which fires on every call, on every thread —
  never contends on a lock
- **Raw monitor**: at `ThreadEnd` each thread's count is merged into a global total under
  the lock — one lock acquisition per thread, not one per call. The same lock serializes
  the log file
- **TLS cleanup** in `ThreadEnd`, or the agent leaks one struct per thread
- **Reentrancy guard**: a per-thread flag that stops the callback re-entering itself if
  anything it calls runs Java code

## Build

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Run

```sh
javac AdvancedTestApp.java
java -agentpath:./build/libadvanced_agent.so AdvancedTestApp
```

## Expected Output

`advanced_agent.log` (JDK 26; counts vary):

```
[Agent] Agent_OnLoad complete — advanced features active.
[ThreadStart] Notification Thread — TLS allocated
[ThreadStart] main — TLS allocated
[ThreadStart] Common-Cleaner — TLS allocated
[ThreadStart] Worker-B — TLS allocated
[ThreadStart] Worker-A — TLS allocated
[ThreadStart] Worker-C — TLS allocated
[Sample] Worker-C: entry #10000 is Ljdk/internal/util/DecimalDigits;.uncheckedPutPairLatin1
...
[ThreadEnd] Worker-C — 26746 method entries
[ThreadEnd] Worker-B — 20924 method entries
[ThreadEnd] Worker-A — 27557 method entries
[ThreadEnd] main — 9587 method entries
[ThreadStart] DestroyJavaVM — TLS allocated
[ThreadEnd] DestroyJavaVM — 11 method entries
[Agent] VMDeath — 84825 method entries across 5 finished threads
```

The total is exactly the sum of the per-thread counts — the CI check verifies that. Each
worker calls `doWork` only 500 times, yet records over 20,000 method entries: nearly all of
them are JDK methods running underneath (`String.valueOf`, `trim`, ...).
