# Lab 04b — Virtual Threads (JDK 21+)

> **Book:** companion to [Chapter 4: Inspecting Threads, Stacks, and Methods](https://github.com/sachin-handiekar/learning-jvmti/blob/master/manuscript/chapter4.md) (§4.7) of *Learning JVMTI*. Read the chapter for the concepts; build and run the code here.

Lab 4's thread dump finds every thread with `GetAllThreads`. Run it against an application
that uses virtual threads and it misses almost all of them. This lab shows what changes,
by watching 11,000 virtual threads:

- **`ThreadStart`/`ThreadEnd` don't fire for virtual threads.** The dedicated
  `VirtualThreadStart`/`VirtualThreadEnd` events do, once the agent has
  `can_support_virtual_threads` — part of JVMTI 21, so the agent asks `GetEnv` for
  `JVMTI_VERSION_21`
- **`GetAllThreads` returns platform threads only.** It includes the *carrier* threads
  virtual threads run on (`ForkJoinPool-1-worker-N`), never the virtual threads themselves
- **`GetThreadState` and `GetStackTrace` work on a virtual thread** — even an unmounted one
  that is parked and not running on any carrier — if you hold its `jthread`
- **Bookkeeping stays cheap**: counters plus three sample threads, not state per virtual
  thread. Applications can have millions of them

`VirtualThreadTestApp` starts 10,000 virtual threads that park forever, plus 1,000 that
finish immediately. Virtual threads are always daemon threads, so when `main` returns, the
parked ones are still alive for the agent to inspect at `VMDeath`.

**Requires JDK 21 or newer** to build (the virtual-thread API is in `jvmti.h` from JDK 21)
and to run. On an older JVM the agent refuses to load with a clear message.

## Build

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Run

```sh
javac VirtualThreadTestApp.java
java -agentpath:$PWD/build/libvirtual_threads_agent.so VirtualThreadTestApp
```

## Expected Output

(JDK 26; the platform-thread and carrier counts vary with the JDK and CPU count.)

```
[VThreads] Agent loaded (Lab 04b): tracking virtual threads
VirtualThreadTestApp started
  10000 virtual threads parked, 1000 finished
VirtualThreadTestApp finished

[VThreads] === Report at VMDeath ===
[VThreads] ThreadStart events:        13   (platform threads only)
[VThreads] VirtualThreadStart events: 11000
[VThreads] VirtualThreadEnd events:   1000
[VThreads] Virtual threads alive:     10000
[VThreads] GetAllThreads returned:    16 threads: 0 virtual, 8 carrier (ForkJoinPool-*)
[VThreads] Sample "parked-0": virtual=yes state=WAITING
      at Ljava/lang/VirtualThread;.yieldContinuation
      at Ljava/lang/VirtualThread;.park
      at Ljava/lang/System$1;.parkVirtualThread
      at Ljava/util/concurrent/locks/LockSupport;.park
      at Ljava/util/concurrent/locks/AbstractQueuedSynchronizer;.acquire
      at Ljava/util/concurrent/locks/AbstractQueuedSynchronizer;.acquireSharedInterruptibly
      at Ljava/util/concurrent/CountDownLatch;.await
      at LVirtualThreadTestApp;.waitForever
      at LVirtualThreadTestApp;.lambda$main$0
      ...
[VThreads] Sample "parked-1": virtual=yes state=WAITING
[VThreads] Sample "parked-2": virtual=yes state=WAITING
```

11,000 virtual threads started, yet `ThreadStart` fired just 13 times and `GetAllThreads`
returned 16 threads — none of them virtual.
