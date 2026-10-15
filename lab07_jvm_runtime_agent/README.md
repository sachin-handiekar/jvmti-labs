# Lab 07 — JVM Runtime Interaction Agent

> **Book:** companion to [Chapter 7: Interacting with the JVM Runtime](https://github.com/sachin-handiekar/learning-jvmti/blob/master/manuscript/chapter7.md) of *Learning JVMTI*. Read the chapter for the concepts; build and run the code here.

Demonstrates JVM runtime inspection and GC monitoring:

- **GC events**: `GarbageCollectionStart` / `GarbageCollectionFinish` timed with
  `GetTime` (nanosecond wall-clock). GC callbacks run in a restricted environment —
  no JNI, no file I/O — so they do bookkeeping only; the summary is printed at `VMDeath`
- **System properties**: Reading `java.vm.name`, `java.vm.version`, etc. JVMTI only sees the
  properties the VM itself defines; `os.name` or `java.version` come back empty
- **Runtime queries**: `GetAvailableProcessors`, `GetPhase`, live-thread count
- **Per-thread CPU time** (§7.4): `GetCurrentThreadCpuTime` in the `ThreadEnd` callback,
  which runs on the ending thread. The test app's `CpuBurner` and `Sleeper` threads run for
  the same wall time but use very different CPU time
- **Object sizing**: Using `GetObjectSize` to measure object memory footprint

> Run with a small heap (e.g., `java -Xmx16m ...`) to force collections and see
> GC statistics in the summary.

## Build

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Run

```sh
javac RuntimeTestApp.java
java -agentpath:./build/libjvm_runtime_agent.so RuntimeTestApp
```

## Expected Output

`jvm_runtime_agent.log` (JDK 26 on Windows; values vary):

```
[Runtime] Agent loaded — monitoring GC and runtime.
[Runtime] VMInit — printing JVM runtime info...
[Runtime] java.vm.name = Java HotSpot(TM) 64-Bit Server VM
[Runtime] java.vm.version = 26.0.1+8-34
[Runtime] java.vm.vendor = Oracle Corporation
[Runtime] java.home = C:\Program Files\Java\jdk-26.0.1
[Runtime] processors = 8
[Runtime] phase = LIVE
[Runtime] live threads = 5
[Runtime] Shallow size of a String instance: 24 bytes
[CPU] thread "CpuBurner" used 296.875 ms of CPU
[CPU] thread "Sleeper" used 0.000 ms of CPU
[CPU] thread "main" used 46.875 ms of CPU
[CPU] thread "DestroyJavaVM" used 46.875 ms of CPU
[GC] 10 collections, total 21.724 ms, max 2.887 ms, avg 2.172 ms
```

CPU times are only as fine-grained as the OS clock: Windows reports them in 15.6 ms steps.
