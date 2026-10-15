/**
 * DebugTestApp.java — Companion test app for Lab 08b (the mini debugger).
 *
 * Compile with -g so the class carries line-number and local-variable
 * tables; the debugger needs both.
 *
 *   javac -g DebugTestApp.java
 *
 * The agent sets a breakpoint on line 21 (the "int discount" line) and
 * watches the static field "balance".
 */
public class DebugTestApp {

    static int balance = 100;   // watched by the agent

    static int checkout(int[] prices, String customer) {
        int total = 0;
        for (int price : prices) {
            total += price;
        }
        int discount = total > 50 ? 10 : 0;
        int charged = total - discount;
        System.out.println("  " + customer + " pays " + charged);
        return charged;
    }

    public static void main(String[] args) {
        System.out.println("DebugTestApp started");
        int charged = checkout(new int[] { 20, 30, 15 }, "alice");
        balance -= charged;
        balance += 5;   // refund
        System.out.println("DebugTestApp finished, balance = " + balance);
    }
}
