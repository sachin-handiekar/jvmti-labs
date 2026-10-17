import java.util.ArrayList;
import java.util.List;

/**
 * OomTestApp.java — test app for the Lab 12 out-of-memory exercise.
 *
 * Run with a small heap (-Xmx32m): it keeps 1 MB blocks until the heap
 * is exhausted, catches the OutOfMemoryError, frees everything and
 * carries on — the agent sees the moment it happens.
 */
public class OomTestApp {
    public static void main(String[] args) {
        System.out.println("OomTestApp: filling the heap...");
        List<byte[]> blocks = new ArrayList<>();
        try {
            while (true) {
                blocks.add(new byte[1024 * 1024]);
            }
        } catch (OutOfMemoryError e) {
            int held = blocks.size();
            blocks = null;   // let it all go before doing anything else
            System.out.println("OomTestApp: caught OutOfMemoryError after " + held + " MB");
        }
        System.out.println("OomTestApp: still running, finished normally");
    }
}
