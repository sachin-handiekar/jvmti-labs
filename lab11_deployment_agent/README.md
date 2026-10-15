# Lab 11 — Deploying and Testing JVMTI Agents

> **Book:** companion to [Chapter 11: Deploying and Testing JVMTI Agents](https://github.com/sachin-handiekar/learning-jvmti/blob/master/manuscript/chapter11.md) of *Learning JVMTI*. Read the chapter for the concepts; build and run the code here.

A configurable, production-style JVMTI agent demonstrating deployment concerns:

- **Both entry points**: `Agent_OnLoad` (startup) and `Agent_OnAttach` (live attach,
  §11.4) share one initialization path — the best-practice from §11.6
- **Runtime options** controlling which events to enable and which package to monitor
- **Thread-safe file logging with rotation** (`prod_agent.log`, raw-monitor protected —
  event callbacks fire concurrently on every Java thread)
- **Graceful degradation**: `GetPotentialCapabilities` is consulted before requesting
  capabilities, since some (like method entry/exit) may be unavailable at attach time

## Building

```sh
mkdir build && cd build
cmake ..
cmake --build .
```

## Usage

### Load at startup

```sh
javac -d . DeployTestApp.java
java -agentpath:./build/libprod_agent.so=package=com/example,method_entry,thread_events \
     -cp . com.example.DeployTestApp
```

(On Windows use `build\prod_agent.dll`, on macOS `build/libprod_agent.dylib`.)

### Attach to a running JVM (§11.4)

Start the app in `--wait` mode: it keeps starting worker threads for the given number of
seconds and prints its PID. Then, from a second terminal, load the agent with either the
book's `AgentLoader` (Attach API) or `jcmd`:

```sh
javac -d . DeployTestApp.java AgentLoader.java
java -XX:+EnableDynamicAgentLoading -cp . com.example.DeployTestApp --wait 30

# second terminal — either of these:
java -cp . AgentLoader <pid> /abs/path/build/libprod_agent.so package=com/example,thread_events,method_entry
jcmd <pid> JVMTI.agent_load /abs/path/build/libprod_agent.so package=com/example,thread_events
```

`prod_agent.log` then shows the JVM calling `Agent_OnAttach` and the agent degrading
gracefully. On HotSpot, `method_entry` can't be added once the JVM is running:

```
[JVMTI] method_entry unavailable on this JVM/phase — disabled.
[JVMTI] Production agent v1.1.0 attached to running JVM (package=com/example entry=0 exit=0 threads=1)
[ThreadStart] DeployWorker-4
[ThreadEnd] DeployWorker-4
...
```

> **JDK 21+ note:** dynamic attach prints a warning by default (JEP 451); the
> `-XX:+EnableDynamicAgentLoading` flag above suppresses it. The flag doesn't exist before
> JDK 21 — on older JDKs omit it (or add `-XX:+IgnoreUnrecognizedVMOptions`).

### Available options

| Option | Effect |
|--------|--------|
| `package=<prefix>` | Only monitor methods whose class name starts with the prefix (default: `com/example`) |
| `method_entry` | Enable method entry logging |
| `method_exit` | Enable method exit logging |
| `thread_events` | Enable thread start/end logging |

## Log Rotation

Logs are written to `prod_agent.log`. When the file exceeds 10 KB (demo value), it is
rotated to `prod_agent.log.1` … `.3`.

## Overhead Warning

`method_entry`/`method_exit` fire for **every** call and are expensive — they exist here
to demonstrate configurability. For CPU profiling, use the sampling approach from Lab 10 instead.
