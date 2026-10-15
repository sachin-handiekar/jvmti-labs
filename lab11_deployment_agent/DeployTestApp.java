package com.example;

/**
 * DeployTestApp.java — Companion test app for Lab 11.
 *
 * Lives in the com.example package so it matches the agent's default
 * package filter. Creates threads and calls methods to exercise the
 * configurable production agent.
 *
 * Compile:  javac -d . DeployTestApp.java
 *
 * Load at startup:
 *   java -agentpath:./build/libprod_agent.so=package=com/example,method_entry,thread_events \
 *        -cp . com.example.DeployTestApp
 *
 * Attach later (§11.4) — keep the app running with --wait <seconds>,
 * then load the agent into it with jcmd or AgentLoader:
 *   java -XX:+EnableDynamicAgentLoading -cp . com.example.DeployTestApp --wait 30
 */
public class DeployTestApp {

    static int compute(int n) {
        int total = 0;
        for (int i = 0; i < n; i++) total += i;
        return total;
    }

    static void runWorker(String name) throws InterruptedException {
        Thread worker = new Thread(() -> {
            System.out.println("  " + name + " computed: " + compute(1000));
        }, name);
        worker.start();
        worker.join();
    }

    public static void main(String[] args) throws InterruptedException {
        System.out.println("DeployTestApp started");

        if (args.length == 2 && args[0].equals("--wait")) {
            // Long-running mode: start a new worker thread every 500ms so
            // an agent attached mid-run has events to observe
            long seconds = Long.parseLong(args[1]);
            System.out.println("DeployTestApp waiting " + seconds
                    + "s for an agent to attach (PID " + ProcessHandle.current().pid() + ")");
            long end = System.nanoTime() + seconds * 1_000_000_000L;
            for (int i = 1; System.nanoTime() < end; i++) {
                runWorker("DeployWorker-" + i);
                Thread.sleep(500);
            }
        } else {
            runWorker("DeployWorker");
        }

        System.out.println("DeployTestApp finished — see prod_agent.log");
    }
}
