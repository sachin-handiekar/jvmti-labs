# Lab 08 — Exception Handling Agent

> **Book:** companion to [Chapter 8: Exception Handling and Debugging](https://github.com/sachin-handiekar/learning-jvmti/blob/master/manuscript/chapter8.md) of *Learning JVMTI*. Read the chapter for the concepts; build and run the code here.

Monitors and logs all Java exceptions with detailed context:

- **Exception callback** with caught/uncaught classification
- **Throw and catch location** details (class, method, BCI)
- **Stack trace capture** at exception throw site
- **Application-focused filtering**: an exception is logged if it is thrown in application
  code, *caught* in application code, or uncaught. Filtering on the throw site alone would
  hide e.g. the `NumberFormatException` that `Integer.parseInt` throws from inside `java/lang`
- **Exception message extraction** via JNI

## Build

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Run

```sh
javac ExceptionTestApp.java
java -agentpath:./build/libexception_agent.so ExceptionTestApp
```

## Expected Output

`ExceptionTestApp` throws five caught exceptions (including a `NumberFormatException` from
inside `Integer.parseInt`) and one **uncaught** exception on a separate `Doomed` thread. That
thread dies with the usual stack trace on stderr, but the JVM exits normally.

```
[Exception] Summary: 6 thrown (5 caught, 1 uncaught) including JDK-internal exceptions
```

`exception_agent.log` has one entry per exception, for example:

```
[Exception] CAUGHT Ljava/lang/NumberFormatException;: For input string: "not-a-number"
  Thrown at: Ljava/lang/Integer;.parseInt (bci=284)
  Caught at: LExceptionTestApp;.main (bci=61)
  ...
[Exception] UNCAUGHT Ljava/lang/ArrayIndexOutOfBoundsException;: Index 10 out of bounds for length 3
  Thrown at: LExceptionTestApp;.lambda$main$0 (bci=9)
  ...
```

"Uncaught" means no handler exists anywhere on the throwing thread's stack (`catch_method`
is `NULL`), so that thread is about to die.
