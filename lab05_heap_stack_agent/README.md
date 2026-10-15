# Lab 05 — Heap Histogram and Stack Analysis Agent

> **Book:** companion to [Chapter 5: Heap Inspection and Object Tagging](https://github.com/sachin-handiekar/learning-jvmti/blob/master/manuscript/chapter5.md) of *Learning JVMTI*. Read the chapter for the concepts; build and run the code here.

At `VMDeath`, the agent produces a **heap histogram** — the view `jmap -histo` gives you —
and then dumps the live threads' stacks:

- **Class tagging** (§5.3): every loaded class is tagged with its index via `SetTag`
- **One heap walk** with `IterateThroughHeap` (§5.2): the JVM hands the callback each
  object's *class* tag, so objects are grouped by class with no per-object lookups
- **Top 10 classes by shallow size**, with object counts
- **Thread stacks** with `GetAllThreads` + `GetStackTrace`, pairing "what is on the heap"
  with "what the threads are doing" (§5.7)
- Requires the `can_tag_objects` capability, the prerequisite for all heap functions

`HeapStackTestApp` simulates a leak: a static cache that keeps 20,000 `CacheEntry` objects
alive. Three daemon worker threads park a few frames deep, so they are still alive when
`VMDeath` fires.

> The older JVMTI 1.0 heap functions (`IterateOverHeap`, `IterateOverReachableObjects`)
> are deprecated — and their reference callbacks fire once per *reference*, not per
> object, which silently miscounts. Use `IterateThroughHeap`; for reachability and
> "who holds this alive?" analysis, use `FollowReferences` (§5.4).

## Build

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Run

```sh
javac HeapStackTestApp.java
java -agentpath:./build/libheap_stack_agent.so HeapStackTestApp
```

(On Windows use `build\heap_stack_agent.dll`, on macOS `build/libheap_stack_agent.dylib`.)

## Expected Output

`heap_stack_agent.log` (JDK 26; numbers vary by JDK and garbage collector):

```
[HeapStack] Agent loaded — heap/stack analysis ready.
[Heap] Walking the heap...
[Heap] 112417 objects, 5410176 bytes in 813 classes
[Heap] Top classes by shallow size:
  rank     objects        bytes  class
     1       49154      2583488  [B
     2       28978       695472  Ljava/lang/String;
     3          48       519368  [Ljdk/internal/vm/FillerElement;
     4       20000       480000  LHeapStackTestApp$CacheEntry;
     5        1558       411480  [Ljava/lang/Object;
     ...
[Stacks] 10 live threads:
  ...
  Thread: Worker-0
    at Ljava/lang/Thread;.sleepNanos0 (bci=-1)
    at Ljava/lang/Thread;.sleepNanos (bci=33)
    at Ljava/lang/Thread;.sleep (bci=25)
    at LHeapStackTestApp;.recursiveWork (bci=7)
    at LHeapStackTestApp;.recursiveWork (bci=19)
    ...
```

The leak is easy to spot: exactly 20,000 `CacheEntry` objects. The `byte[]` and `String`
rows above it include the leak's own keys and payloads. The histogram counts *every*
object still in the heap, garbage included — the walk doesn't run a GC first — so treat it
as "what's taking space", not "what's reachable".
