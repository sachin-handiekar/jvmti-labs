/**
 * HeapStackTestApp.java — Companion test app for Lab 05.
 *
 * Simulates a leak: a static cache keeps 20,000 CacheEntry objects (each
 * holding a String and a 64-byte buffer) alive, so they stand out in the
 * agent's heap histogram. Three daemon worker threads park a few frames
 * deep, so they are still alive — with interesting stacks — when the
 * agent runs its analysis at VMDeath.
 */
import java.util.ArrayList;
import java.util.List;

public class HeapStackTestApp {

    static class CacheEntry {
        final String key;
        final byte[] payload;
        CacheEntry(String key) {
            this.key = key;
            this.payload = new byte[64];
        }
    }

    // Never cleared — the "leak" the histogram should reveal
    static final List<CacheEntry> CACHE = new ArrayList<>();

    public static void main(String[] args) throws InterruptedException {
        System.out.println("HeapStackTestApp: Filling the cache...");
        for (int i = 0; i < 20_000; i++) {
            CACHE.add(new CacheEntry("key-" + i));
        }

        // Daemon threads don't keep the JVM alive, so they are still
        // running (parked inside recursiveWork) when VMDeath fires
        for (int i = 0; i < 3; i++) {
            final int id = i;
            Thread w = new Thread(() -> recursiveWork(id, 5), "Worker-" + i);
            w.setDaemon(true);
            w.start();
        }
        Thread.sleep(200);  // let the workers reach the bottom

        System.out.println("HeapStackTestApp: Done (" + CACHE.size()
                + " cache entries). Check heap_stack_agent.log.");
    }

    static void recursiveWork(int id, int depth) {
        if (depth <= 0) {
            try { Thread.sleep(60_000); } catch (InterruptedException e) { }
            return;
        }
        recursiveWork(id, depth - 1);
    }
}
