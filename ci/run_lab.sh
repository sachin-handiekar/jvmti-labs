#!/usr/bin/env bash
#
# run_lab.sh — run one lab's agent against its test app and verify the output.
#
# Usage:  ci/run_lab.sh <lab_dir> [agent_library]
#
# Compiles the lab's Java test app, runs it with the agent, and checks the
# agent's output (stdout/stderr, log files, reports) for the behaviour the
# lab is meant to demonstrate. Exits non-zero on the first failed check.
# If agent_library is omitted, the built library is found under <lab_dir>/build.

set -euo pipefail

LAB="${1:?usage: run_lab.sh <lab_dir> [agent_library]}"
cd "$(dirname "$0")/../$LAB"

AGENT="${2:-}"
if [ -z "$AGENT" ]; then
    AGENT=$(find build -type f \( -name '*.so' -o -name '*.dylib' -o -name '*.dll' \) | head -1)
fi
[ -n "$AGENT" ] && [ -f "$AGENT" ] || { echo "FAIL: no agent library for $LAB"; exit 1; }
AGENT=$(cd "$(dirname "$AGENT")" && pwd)/$(basename "$AGENT")
if command -v cygpath >/dev/null 2>&1; then AGENT=$(cygpath -w "$AGENT"); fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cp ./*.java "$WORK"/ 2>/dev/null || true
cd "$WORK"
shopt -s nullglob
java_files=(*.java)
if [ ${#java_files[@]} -gt 0 ]; then
    javac -g -d . "${java_files[@]}"
fi

# run <agent options> <java args...> — stdout+stderr captured in out.txt
run() {
    local opts="$1"; shift
    local agentarg="-agentpath:$AGENT"
    [ -n "$opts" ] && agentarg="$agentarg=$opts"
    echo "+ java $agentarg $*"
    set +e
    timeout 120 java "$agentarg" "$@" > out.txt 2>&1
    local rc=$?
    set -e
    cat out.txt
    echo "(exit code $rc)"
    if [ "$rc" -ne 0 ] && [ "${ALLOW_FAIL:-0}" != 1 ]; then
        echo "FAIL: JVM exited with $rc"; exit 1
    fi
}

# expect <file> <extended regex> — the file must contain a matching line
expect() {
    if grep -Eq -- "$2" "$1"; then
        echo "ok: $1 matches /$2/"
    else
        echo "FAIL: $1 has no line matching /$2/"
        echo "---- $1 ----"; cat "$1" || true
        exit 1
    fi
}

# reject <file> <extended regex> — the file must NOT contain a matching line
reject() {
    if grep -Eq -- "$2" "$1"; then
        echo "FAIL: $1 unexpectedly matches /$2/"
        echo "---- $1 ----"; cat "$1"
        exit 1
    fi
    echo "ok: $1 does not match /$2/"
}

case "$LAB" in
lab01_capability_checker)
    run "" -version
    expect out.txt '^can_tag_objects: [01]$'
    ;;
lab02_basic_agent)
    run "verbose=true,output=custom.log" HelloApp
    expect out.txt 'Verbose mode enabled'
    expect out.txt 'Logging to custom.log'
    expect custom.log 'JVM version: '
    expect custom.log 'VMDeath event'
    ;;
lab03_event_registration)
    run "" EventTestApp
    expect out.txt 'VMDeath\. Total method entries: [1-9][0-9]*'
    for i in 0 1 2 3 4; do
        expect event_logger_agent.log "Thread started: EventWorker-$i$"
        expect event_logger_agent.log "Thread ended: EventWorker-$i$"
    done
    ;;
lab04_thread_class_inspection)
    run "" StackTestApp
    L=thread_class_inspection.log
    expect $L '^"Sleeper" TIMED_WAITING'
    expect $L 'StackTestApp;\.methodC\(\)V \(line [0-9]+\)'
    expect $L '^"Waiter" WAITING'
    expect $L '^"TimedWaiter" TIMED_WAITING'   # wait(ms): not SLEEPING
    expect $L '^"Blocked" BLOCKED'
    ;;
lab04b_virtual_threads)
    run "" VirtualThreadTestApp
    expect out.txt '^\[VThreads\] VirtualThreadStart events: 11000$'
    expect out.txt '^\[VThreads\] VirtualThreadEnd events:   1000$'
    expect out.txt '^\[VThreads\] Virtual threads alive:     10000$'
    # ThreadStart never fires for virtual threads: only a handful of platform threads
    expect out.txt '^\[VThreads\] ThreadStart events: +[0-9]{1,3} '
    # GetAllThreads returns no virtual threads, but does include carriers
    expect out.txt 'GetAllThreads returned: +[0-9]+ threads: 0 virtual, [1-9][0-9]* carrier'
    # Stacks of unmounted virtual threads are readable
    expect out.txt '^\[VThreads\] Sample "parked-[0-9]+": virtual=yes state=WAITING$'
    expect out.txt 'at LVirtualThreadTestApp;\.waitForever$'
    ;;
lab05_heap_stack_agent)
    run "" HeapStackTestApp
    L=heap_stack_agent.log
    expect $L '^\[Heap\] [1-9][0-9]* objects, [1-9][0-9]* bytes in [1-9][0-9]* classes'
    expect $L ' 20000 +[0-9]+  LHeapStackTestApp\$CacheEntry;$'   # the leak, via class tags
    expect $L 'at LHeapStackTestApp;\.recursiveWork'               # daemon worker stacks
    ;;
lab06_class_transform_agent)
    run "" TransformTestApp
    expect out.txt 'Loading: TransformTestApp\$CasualGreeter'
    reject out.txt 'Loading: java/'
    expect out.txt 'Total classes loaded: [1-9][0-9]*'
    ;;
lab06b_class_patcher)
    # 1. Load time: the class is patched as it loads
    run "" PatchTestApp
    expect out.txt '^\[Patcher\] Patched PatchTestApp: '
    expect out.txt '^  \[1\] Hello from JVMTI: this class was patched in memory$'
    reject out.txt '^  \[1\] Hello from the original class file$'

    # 2. Attach + RetransformClasses: output changes mid-run
    mkdir attach && cd attach
    java -XX:+IgnoreUnrecognizedVMOptions -XX:+EnableDynamicAgentLoading \
         -cp .. PatchTestApp --loop 12 > app.txt 2>&1 &
    APP=$!
    PID=""
    for _ in $(seq 1 60); do
        PID=$(jps -l | awk '$2 == "PatchTestApp" {print $1}')
        [ -n "$PID" ] && break
        sleep 0.25
    done
    [ -n "$PID" ] || { echo "FAIL: PatchTestApp did not start"; cat app.txt; exit 1; }
    sleep 1.5   # let a few unpatched lines print first
    echo "+ java AgentLoader $PID <agent>"
    java -cp .. AgentLoader "$PID" "$AGENT"
    wait $APP
    cat app.txt
    expect app.txt '^  \[1\] Hello from the original class file$'
    expect app.txt '^\[Patcher\] Retransformed PatchTestApp: '
    expect app.txt '^  \[12\] Hello from JVMTI: this class was patched in memory$'
    ;;
lab07_jvm_runtime_agent)
    run "" RuntimeTestApp
    expect out.txt '^\[GC\] [1-9][0-9]* collections'
    expect jvm_runtime_agent.log 'java\.vm\.name = '
    expect jvm_runtime_agent.log 'Shallow size of a String instance: [1-9][0-9]* bytes'
    expect jvm_runtime_agent.log '^\[CPU\] thread "CpuBurner" used [1-9][0-9.]* ms'
    expect jvm_runtime_agent.log '^\[CPU\] thread "Sleeper" used'
    ;;
lab08_exception_agent)
    run "" ExceptionTestApp
    L=exception_agent.log
    expect $L 'CAUGHT Ljava/lang/IllegalArgumentException;'
    expect $L 'CAUGHT Ljava/lang/NumberFormatException;'   # thrown inside the JDK, caught by the app
    expect $L 'Caught at: LExceptionTestApp;\.main'
    expect $L 'UNCAUGHT Ljava/lang/ArrayIndexOutOfBoundsException;'   # Doomed thread
    ;;
lab09_advanced_techniques)
    run "" AdvancedTestApp
    L=advanced_agent.log
    for w in A B C; do
        expect $L "^\\[ThreadEnd\\] Worker-$w — [1-9][0-9]* method entries"
    done
    # The global total must be exactly the sum of the per-thread counts
    sum=$(grep '^\[ThreadEnd\]' $L | sed 's/.* — \([0-9]*\) method entries/\1/' | awk '{s+=$1} END{print s}')
    expect $L "^\\[Agent\\] VMDeath — $sum method entries"
    ;;
lab10_profiler_agent)
    run "" ProfilerTestApp
    P=profiler_output.txt
    expect $P '^ProfilerTestApp\.main;.*ProfilerTestApp\.hotMethod [0-9]+$'
    reject $P 'ReferenceHandler|FinalizerThread|CleanerImpl'   # idle threads filtered
    reject $P '^[^ ]*;[^ ;]*;\.'                                  # no ';' inside class names
    ;;
lab11_deployment_agent)
    run "package=com/example,method_entry,thread_events" -cp . com.example.DeployTestApp
    L=prod_agent.log
    expect $L 'loaded at startup \(package=com/example entry=1 exit=0 threads=1\)'
    expect $L '^\[Entry\] Lcom/example/DeployTestApp;\.compute\(I\)I'
    expect $L '^\[ThreadStart\] DeployWorker'
    reject $L '^\[Entry\] Ljava/'

    # Live attach (§11.4): run the app in --wait mode, then load the agent
    # into it with AgentLoader. EnableDynamicAgentLoading only exists on
    # JDK 21+, hence IgnoreUnrecognizedVMOptions for older JDKs.
    mkdir attach && cd attach
    java -XX:+IgnoreUnrecognizedVMOptions -XX:+EnableDynamicAgentLoading \
         -cp .. com.example.DeployTestApp --wait 5 > app.txt 2>&1 &
    APP=$!
    PID=""
    for _ in $(seq 1 60); do
        PID=$(jps -l | awk '/com\.example\.DeployTestApp/ {print $1}')
        [ -n "$PID" ] && break
        sleep 0.25
    done
    [ -n "$PID" ] || { echo "FAIL: DeployTestApp did not start"; cat app.txt; exit 1; }
    echo "+ java AgentLoader $PID <agent> package=com/example,thread_events"
    java -cp .. AgentLoader "$PID" "$AGENT" "package=com/example,thread_events"
    wait $APP
    cat app.txt
    expect prod_agent.log 'attached to running JVM \(package=com/example .*threads=1\)'
    expect prod_agent.log '^\[ThreadStart\] DeployWorker-[0-9]+'
    ;;
lab12_stability_agent)
    run "" StabilityTestApp
    expect out.txt 'VMDeath — [0-9]+ exceptions observed'
    if [ "$(uname -s)" = Linux ] || [ "$(uname -s)" = Darwin ]; then
        expect out.txt "SIGSEGVs chained to the JVM's handler: [0-9]+"
    fi
    ;;
lab13_allocation_tracker)
    run "output=report.json" AllocTestApp
    expect report.json '"sampled_allocations": [1-9][0-9]*'
    expect report.json '"class": "\[B"'
    reject report.json '"class": "\[C"'
    ;;
*)
    echo "FAIL: no checks defined for $LAB"; exit 1 ;;
esac

echo "PASS: $LAB"
