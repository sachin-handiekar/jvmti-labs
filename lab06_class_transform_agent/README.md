# Lab 6 — Class Transformation and Bytecode Instrumentation

A class-load monitor (§6.7) built on the **ClassFileLoadHook** event, which the JVM calls
with the raw class-file bytes of every class *before* it is defined — the same hook every
bytecode-instrumentation agent uses.

## Key Concepts

- Registering a `ClassFileLoadHook` callback (no capability needed for load-time hooking)
- Requesting `can_generate_all_class_hook_events` so the hook also sees early-loaded classes
- Filtering out `java/*`, `jdk/*` and `sun/*` classes (§6.3 — never transform these)
- Guarding shared counters with a raw monitor, since classes load concurrently
- Printing a class-count / bytecode-size summary at `VMDeath`

## Files

| File | Description |
|------|-------------|
| `class_transform_agent.c` | The JVMTI agent (C shared library) |
| `TransformTestApp.java` | Companion test app from §6.7 (loads a few nested classes) |
| `CMakeLists.txt` | Build configuration |

## Build

```sh
cmake -S . -B build
cmake --build build
```

## Run

```sh
javac TransformTestApp.java
java -agentpath:./build/libclass_transform_agent.so TransformTestApp
```

(On Windows use `build\class_transform_agent.dll`, on macOS the `.dylib`.)

## Expected Output

```
[Agent] Class load monitor agent loaded (Lab 06)
[Agent] Loading: TransformTestApp                                   ( 1207 bytes)
TransformTestApp started
[Agent] Loading: TransformTestApp$Greeter                           (  260 bytes)
[Agent] Loading: TransformTestApp$FormalGreeter                     (  845 bytes)
[Agent] Loading: TransformTestApp$CasualGreeter                     (  840 bytes)
Good day, World
Hey World!
TransformTestApp finished

[Agent] === Class Loading Summary ===
[Agent] Total classes loaded: 251
[Agent] Total bytecode size:  1345397 bytes
```

Byte sizes and totals vary by JDK version and vendor.

## Notes

This example **observes** class loading but does not modify any bytecode.
To actually instrument it, you would:

1. Use a bytecode library (e.g., [ASM](https://asm.ow2.io/)) to inject instrumentation
2. Allocate the output buffer with `(*jvmti)->Allocate()` — **never** `malloc()`;
   the JVM deallocates it for you (§6.4)
3. Set `*new_class_data` and `*new_class_data_len` to return the modified bytes

See §6.5 for the full JNI-to-ASM bridge, including the mandatory
`ExceptionCheck`/`ExceptionClear` handling and the `VM_INIT` phase guard.
