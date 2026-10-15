import java.util.*;

public class TransformTestApp {
    interface Greeter { String greet(String name); }

    static class FormalGreeter implements Greeter {
        public String greet(String name) { return "Good day, " + name; }
    }

    static class CasualGreeter implements Greeter {
        public String greet(String name) { return "Hey " + name + "!"; }
    }

    public static void main(String[] args) {
        System.out.println("TransformTestApp started");

        Greeter[] greeters = { new FormalGreeter(), new CasualGreeter() };
        for (Greeter g : greeters) {
            System.out.println(g.greet("World"));
        }

        // Force loading some extra classes
        Map<String, Integer> map = new HashMap<>();
        map.put("test", 42);

        System.out.println("TransformTestApp finished");
    }
}
