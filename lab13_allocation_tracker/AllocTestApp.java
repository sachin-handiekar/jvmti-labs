import java.util.*;

public class AllocTestApp {

    /* Generate many small objects */
    static void smallAllocations() {
        List<String> strings = new ArrayList<>();
        for (int i = 0; i < 100_000; i++) {
            strings.add("item-" + i);
        }
        System.out.println("  Created " + strings.size() + " strings");
    }

    /* Generate fewer large objects */
    static void largeAllocations() {
        List<byte[]> buffers = new ArrayList<>();
        for (int i = 0; i < 1_000; i++) {
            buffers.add(new byte[8192]);
        }
        System.out.println("  Created " + buffers.size() + " byte arrays (8KB each)");
    }

    /* Generate diverse object types */
    static void diverseAllocations() {
        Map<Integer, String> map = new HashMap<>();
        for (int i = 0; i < 10_000; i++) {
            map.put(i, "value-" + i);
        }
        System.out.println("  Created HashMap with " + map.size() + " entries");
    }

    public static void main(String[] args) {
        System.out.println("AllocTestApp started");

        smallAllocations();
        largeAllocations();
        diverseAllocations();

        System.out.println("AllocTestApp finished");
    }
}
