# Lab 4 — Inspecting Threads, Stacks, and Methods

A thread-dump agent (§4.5) that, at `VMDeath`:

- Enumerates all live threads with `GetAllThreads` / `GetThreadInfo`
- Reports each thread's state via `GetThreadState` — testing `WAITING_WITH_TIMEOUT`
  (Java's `TIMED_WAITING`: `sleep`, `wait(ms)`, `parkNanos`) before the general `WAITING` flag
- Walks each stack with `GetStackTrace` and resolves frames with `GetMethodName`,
  `GetMethodDeclaringClass`, and `GetClassSignature`
- Maps bytecode indices to source lines with `GetLineNumberTable`
  (capability: `can_get_line_numbers`)
- Logs the loaded-class count at `VMInit` as a bonus

Output goes to `thread_class_inspection.log`.

## Building

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage

```sh
javac -g StackTestApp.java     # -g enables line-number tables
java -agentpath:./build/libthread_class_inspection_agent.so StackTestApp
```

(On Windows use `build\thread_class_inspection_agent.dll`, on macOS the `.dylib`.)

## Notes

- Why `VMDeath`? At `VMInit` the application hasn't started, so only JVM threads exist.
  `StackTestApp` starts **daemon** threads in each state (sleeping, `wait()`, `wait(ms)`,
  holding a lock, blocked on it); daemons don't keep the JVM alive, so they are still frozen
  in those states when `VMDeath` fires
- `TimedWaiter` (in `wait(60_000)`) is the case a `SLEEPING`-based check gets wrong — it is
  timed-waiting without sleeping
- The `DestroyJavaVM` thread's stack is empty: it is the thread running the `VMDeath`
  callback, i.e. executing native agent code
- Line numbers require classes compiled with `javac -g`; JDK classes show them because
  the JDK ships debug info
