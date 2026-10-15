# Lab 08b — A Mini Debugger

> **Book:** companion to [Chapter 8: Exception Handling and Debugging](https://github.com/sachin-handiekar/learning-jvmti/blob/master/manuscript/chapter8.md) (§8.6) of *Learning JVMTI*. Read the chapter for the concepts; build and run the code here.

The debugging half of JVMTI — what IntelliJ, Eclipse and `jdb` use underneath, through the
JDK's JDWP agent — in one small agent:

- **Deferred breakpoint.** The target class isn't loaded at startup, so the agent waits
  for `ClassPrepare`, maps a *source line* to a bytecode location with `GetLineNumberTable`,
  and calls `SetBreakpoint` — exactly how a debugger handles a breakpoint you set before
  the class exists
- **Locals at the breakpoint**, from `GetLocalVariableTable` plus `GetLocalInt` /
  `GetLocalObject` — showing only the variables whose scope covers the current location
- **Step over** to the end of the method with `SingleStep`, enabled for **one thread
  only**, reported once per new source line and switched off when the method returns
- **Field watchpoint** with `SetFieldModificationWatch`: every write, with old and new
  value and where it happened — including the field's initializer in `<clinit>`

Capabilities: `can_generate_breakpoint_events`, `can_generate_single_step_events`,
`can_generate_field_modification_events`, `can_access_local_variables`,
`can_get_line_numbers`.

## Build

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Run

Compile the test app with **`-g`**: breakpoints by line need the line-number table, and
local variable names need the local-variable table that only `-g` adds.

```sh
javac -g DebugTestApp.java
java -agentpath:$PWD/build/libmini_debugger.so DebugTestApp
```

## Expected Output

```
[Debugger] Mini debugger loaded (Lab 08b)
[Debugger] Breakpoint set: DebugTestApp.checkout line 21 (bytecode index 35)
[Debugger] Watching writes to DebugTestApp.balance
[Debugger] Watch: DebugTestApp.balance 0 -> 100  (in DebugTestApp.<clinit> line 14)
DebugTestApp started
[Debugger] Breakpoint hit: DebugTestApp.checkout line 21
      Locals in scope:
        prices    int[]  = {20, 30, 15}
        customer  String = "alice"
        total     I      = 65
[Debugger]   step -> line 22
[Debugger]   step -> line 23
  alice pays 55
[Debugger]   step -> line 24
[Debugger] Returned to DebugTestApp.main line 29; stepping off
[Debugger] Watch: DebugTestApp.balance 100 -> 45  (in DebugTestApp.main line 30)
[Debugger] Watch: DebugTestApp.balance 45 -> 50  (in DebugTestApp.main line 31)
DebugTestApp finished, balance = 50
```

Without `-g`, `javac` still emits line numbers, so the breakpoint and stepping work — but
the agent reports `(no local variable table: compile with javac -g)` instead of the locals.
