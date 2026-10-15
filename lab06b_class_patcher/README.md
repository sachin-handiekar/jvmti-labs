# Lab 06b — Class Patcher: Rewriting Bytecode

> **Book:** companion to [Chapter 6: Class Transformation and Bytecode Instrumentation](https://github.com/sachin-handiekar/learning-jvmti/blob/master/manuscript/chapter6.md) (§6.8) of *Learning JVMTI*. Read the chapter for the concepts; build and run the code here.

Lab 6 *watches* classes load. This lab *changes* one: it makes an unmodified Java program
print something else by rewriting a string literal inside its class file — in plain C,
with no bytecode library.

- **`ClassFileLoadHook`** hands the agent the raw class-file bytes before the class is defined
- The agent **parses the constant pool**, finds the `CONSTANT_Utf8` entry that a
  `CONSTANT_String` (a Java string literal) points to, and builds a new class file with that
  one entry replaced. Constant-pool *indices* don't change, so the bytecode (`ldc #n`) and
  everything else in the class stay valid
- The new bytes go back in a buffer from **`jvmti->Allocate`** — never `malloc`; the JVM frees it
- **Two ways to run it**, sharing the same hook:
  - loaded at startup (`Agent_OnLoad`) — the class is patched as it loads
  - attached to a running JVM (`Agent_OnAttach`) — the class is already loaded, so the agent
    calls **`RetransformClasses`** (capability `can_retransform_classes`). The JVM runs the hook
    again on the class's original bytes and swaps in the result; the program's output
    changes mid-run

## Build

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Run: patch at load time

```sh
javac PatchTestApp.java AgentLoader.java
java PatchTestApp                                          # unpatched
java -agentpath:$PWD/build/libclass_patcher.so PatchTestApp
```

```
[Patcher] Loaded at startup; will patch PatchTestApp as it loads
[Patcher] Patched PatchTestApp: "Hello from the original class file" -> "Hello from JVMTI: this class was patched in memory" (1605 -> 1621 bytes)
PatchTestApp started (PID 21124)
  [1] Hello from JVMTI: this class was patched in memory
PatchTestApp finished
```

## Run: patch a running program (attach + retransform)

Start the app so it prints its greeting 10 times, 500 ms apart, and note the PID it prints.
From a second terminal, attach the agent with `AgentLoader` (or
`jcmd <pid> JVMTI.agent_load /abs/path/build/libclass_patcher.so`):

```sh
java -XX:+EnableDynamicAgentLoading PatchTestApp --loop 10

# second terminal:
java AgentLoader <pid> $PWD/build/libclass_patcher.so
```

```
PatchTestApp started (PID 27296)
  [1] Hello from the original class file
  [2] Hello from the original class file
  [3] Hello from the original class file
  [4] Hello from the original class file
[Patcher] Attached to running JVM; retransforming PatchTestApp
[Patcher] Retransformed PatchTestApp: "Hello from the original class file" -> "Hello from JVMTI: this class was patched in memory" (1605 -> 1621 bytes)
  [5] Hello from JVMTI: this class was patched in memory
  ...
```

`-XX:+EnableDynamicAgentLoading` silences the JDK 21+ warning about agents loaded into a
running JVM (JEP 451); the flag doesn't exist before JDK 21, so omit it there.

## Limits

Retransformation can change method *bodies* and constants, but not a class's shape: no
adding or removing fields or methods, no changing signatures or the class hierarchy (§6.6).
A real instrumentation agent would use a bytecode library such as ASM or the JDK's
`java.lang.classfile` API rather than patching bytes by hand — the point here is to see that
a class file is just bytes, and `ClassFileLoadHook` lets you replace them.
