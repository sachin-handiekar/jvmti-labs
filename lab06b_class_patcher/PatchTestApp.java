/**
 * PatchTestApp.java — Companion test app for Lab 06b.
 *
 * Prints a greeting that comes from a plain string literal. The agent
 * rewrites that literal inside the class file, so the same, unchanged
 * program prints something else.
 *
 *   java -agentpath:.../libclass_patcher.so PatchTestApp     patched at load
 *   java PatchTestApp --loop 10                              prints 10 times,
 *       500ms apart — attach the agent meanwhile to see it change mid-run
 */
public class PatchTestApp {

    static String greeting() {
        return "Hello from the original class file";
    }

    public static void main(String[] args) throws InterruptedException {
        int rounds = 1;
        if (args.length == 2 && args[0].equals("--loop")) {
            rounds = Integer.parseInt(args[1]);
        }
        System.out.println("PatchTestApp started (PID " + ProcessHandle.current().pid() + ")");
        for (int i = 1; i <= rounds; i++) {
            System.out.println("  [" + i + "] " + greeting());
            if (i < rounds) Thread.sleep(500);
        }
        System.out.println("PatchTestApp finished");
    }
}
