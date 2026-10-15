public class StackTestApp {
    interface Body { void run() throws InterruptedException; }

    static final Object waitLock  = new Object();
    static final Object timedLock = new Object();
    static final Object heldLock  = new Object();

    // The agent will capture the "Sleeper" thread here, 3 frames deep
    static void methodC() throws InterruptedException { Thread.sleep(60_000); }
    static void methodB() throws InterruptedException { methodC(); }
    static void methodA() throws InterruptedException { methodB(); }

    /* Daemon threads don't keep the JVM alive, so they are still
       running (frozen in their current state) when VM_DEATH fires. */
    static void startDaemon(String name, Body body) {
        Thread t = new Thread(() -> {
            try { body.run(); } catch (InterruptedException e) { }
        }, name);
        t.setDaemon(true);
        t.start();
    }

    public static void main(String[] args) throws InterruptedException {
        System.out.println("StackTestApp started");

        startDaemon("Sleeper", StackTestApp::methodA);              // TIMED_WAITING
        startDaemon("Waiter", () -> {                               // WAITING
            synchronized (waitLock) { waitLock.wait(); }
        });
        startDaemon("TimedWaiter", () -> {                          // TIMED_WAITING
            synchronized (timedLock) { timedLock.wait(60_000); }
        });
        startDaemon("LockHolder", () -> {                           // TIMED_WAITING
            synchronized (heldLock) { Thread.sleep(60_000); }
        });
        Thread.sleep(100);  // let LockHolder grab heldLock first
        startDaemon("Blocked", () -> {                              // BLOCKED
            synchronized (heldLock) { }
        });

        Thread.sleep(200);  // let every daemon reach its state
        System.out.println("StackTestApp finished");
    }
}
