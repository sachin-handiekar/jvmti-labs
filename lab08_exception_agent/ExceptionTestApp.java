/**
 * ExceptionTestApp.java — Companion test app for Lab 08.
 *
 * Throws various exceptions — caught, wrapped, thrown inside the JDK,
 * and one uncaught on its own thread — to exercise the agent.
 */
@SuppressWarnings("unused")
public class ExceptionTestApp {

    public static void main(String[] args) {
        System.out.println("ExceptionTestApp: Testing exception handling...");

        // Test 1: Caught exception
        try {
            riskyOperation();
        } catch (IllegalArgumentException e) {
            System.out.println("  Caught: " + e.getMessage());
        }

        // Test 2: Nested exceptions
        try {
            outerMethod();
        } catch (RuntimeException e) {
            System.out.println("  Caught outer: " + e.getMessage());
        }

        // Test 3: NumberFormatException (caught)
        try {
            Integer.parseInt("not-a-number");  // triggers NumberFormatException
        } catch (NumberFormatException e) {
            System.out.println("  Caught: NumberFormatException");
        }

        // Test 4: NullPointerException (caught)
        try {
            ((String) null).length();  // triggers NullPointerException
        } catch (NullPointerException e) {
            System.out.println("  Caught: NullPointerException");
        }

        // Test 5: uncaught exception (on its own thread). No handler exists
        // anywhere on that thread's stack, so the agent reports UNCAUGHT
        // (catch_method == NULL). The thread dies with the default stack
        // trace on stderr, but main — and the JVM — carry on.
        Thread doomed = new Thread(() -> {
            int[] arr = new int[3];
            arr[10] = 42;  // ArrayIndexOutOfBoundsException
        }, "Doomed");
        doomed.start();
        try { doomed.join(); } catch (InterruptedException e) { }

        System.out.println("ExceptionTestApp: Done. Check exception_agent.log.");
    }

    static void riskyOperation() {
        throw new IllegalArgumentException("test exception from riskyOperation");
    }

    static void outerMethod() {
        try {
            innerMethod();
        } catch (ArithmeticException e) {
            throw new RuntimeException("wrapped: " + e.getMessage(), e);
        }
    }

    static void innerMethod() {
        @SuppressWarnings("unused")
        int x = 1 / 0;  // triggers ArithmeticException
    }
}
