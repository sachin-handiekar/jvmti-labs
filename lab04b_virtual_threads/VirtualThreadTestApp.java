import java.util.concurrent.CountDownLatch;

/**
 * VirtualThreadTestApp.java — Companion test app for Lab 04b (JDK 21+).
 *
 * Starts 10,000 virtual threads that park on a latch that is never
 * released, plus 1,000 that finish straight away. Virtual threads are
 * always daemon threads, so when main returns the parked ones are still
 * alive — and the agent can inspect them at VMDeath.
 */
public class VirtualThreadTestApp {

    static final int PARKED = 10_000;
    static final int SHORT_LIVED = 1_000;
    static final CountDownLatch never = new CountDownLatch(1);

    static void waitForever() {
        try {
            never.await();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    public static void main(String[] args) throws InterruptedException {
        System.out.println("VirtualThreadTestApp started");

        CountDownLatch started = new CountDownLatch(PARKED);
        for (int i = 0; i < PARKED; i++) {
            Thread.ofVirtual().name("parked-" + i).start(() -> {
                started.countDown();
                waitForever();
            });
        }
        started.await();

        Thread[] shortLived = new Thread[SHORT_LIVED];
        for (int i = 0; i < SHORT_LIVED; i++) {
            shortLived[i] = Thread.ofVirtual().name("short-" + i).start(() -> { });
        }
        for (Thread t : shortLived) t.join();

        System.out.println("  " + PARKED + " virtual threads parked, "
                + SHORT_LIVED + " finished");
        System.out.println("VirtualThreadTestApp finished");
    }
}
