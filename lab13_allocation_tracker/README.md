# Lab 13 — Case Study: Allocation Tracker Agent

A complete JVMTI agent that samples object allocations and produces a JSON report.

## Features

- **SampledObjectAlloc event** ([JEP 331](https://openjdk.org/jeps/331), JDK 11+) — sees **all** allocation
  paths, including plain bytecode `new`, on a statistical sampling basis
- **Tunable sampling rate** via `SetHeapSamplingInterval` (64 KB here; the JVM default is 512 KB)
- **Per-class sample counts and byte totals** via a simple lookup table
- **Thread-safe** data access with a raw monitor
- **JSON output** (`alloc_report.json`, or any path via the `output=<path>` agent option) on JVM shutdown

> **Why not `VMObjectAlloc`?** It only fires for allocations the VM performs internally
> (JNI, reflection) — it misses every `new` in Java bytecode, which is nearly all allocation
> in a typical app. See §13.2.

## Build

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Run

```sh
javac AllocTestApp.java
java -agentpath:./build/liballoc_tracker.so AllocTestApp

# Custom report path
java -agentpath:./build/liballoc_tracker.so=output=/tmp/alloc.json AllocTestApp
```

(On Windows use `build\alloc_tracker.dll`, on macOS `build/liballoc_tracker.dylib`.)

## Expected Output

`AllocTestApp` is the companion app from §13.4 (100,000 strings, 1,000 × 8 KB buffers,
a 10,000-entry `HashMap`) — enough allocation to produce a few hundred samples.

```
[Tracker] alloc_tracker loaded (Lab 13 Case Study)
AllocTestApp started
  Created 100000 strings
  Created 1000 byte arrays (8KB each)
  Created HashMap with 10000 entries
AllocTestApp finished
[Tracker] Sampled allocations for 8 classes. See alloc_report.json for details.
```

`alloc_report.json` (excerpt — numbers vary run to run):

```json
{
  "summary": {
    "total_classes": 8,
    "sampled_allocations": 225,
    "sampled_bytes": 2079160,
    "sampling_interval_bytes": 65536
  },
  "classes": [ ... ]
}
```

> **Reading sampled numbers:** These are *samples*, not totals — with a 64 KB interval each
> sample statistically represents ~64 KB of allocation. Exact numbers vary run to run; what's
> stable (and what matters) is the *ranking*: `[B` (byte[]) leads by samples, because every
> `String` stores its characters in a `byte[]` since JDK 9 — you won't see `[C` entries.

## Requirements

- JDK 11 or newer (the agent fails fast with a clear message on older JVMs)
