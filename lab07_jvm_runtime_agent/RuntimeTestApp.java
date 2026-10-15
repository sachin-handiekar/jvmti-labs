/**
 * RuntimeTestApp.java — Companion test app for Lab 07.
 *
 * Creates garbage-collection pressure to trigger GC events, and runs
 * two threads with very different CPU profiles — one busy, one
 * sleeping — so the agent's per-thread CPU-time report has something
 * to compare.
 */
import java.util.ArrayList;
import java.util.List;

public class RuntimeTestApp {
    public static void main(String[] args) throws InterruptedException {
        System.out.println("RuntimeTestApp: Generating GC pressure...");

        for (int round = 0; round < 10; round++) {
            List<byte[]> chunks = new ArrayList<>();
            for (int i = 0; i < 1000; i++) {
                chunks.add(new byte[1024]);  // 1 KB each
            }
            chunks.clear();
            System.gc();  // Suggest GC
            System.out.println("  Round " + (round + 1) + " complete.");
        }

        // ~300ms of wall time each — but very different CPU time
        Thread busy = new Thread(() -> {
            long end = System.nanoTime() + 300_000_000L;
            double x = 0;
            while (System.nanoTime() < end) x += Math.sqrt(x + 1);
            if (x < 0) System.out.println(x);   // keep the loop alive
        }, "CpuBurner");
        Thread idle = new Thread(() -> {
            try { Thread.sleep(300); } catch (InterruptedException e) { }
        }, "Sleeper");
        busy.start();
        idle.start();
        busy.join();
        idle.join();

        System.out.println("RuntimeTestApp: Done. Check jvm_runtime_agent.log.");
    }
}
