/**
 * IntPatchTestApp.java — test app for the Lab 06b int-constant exercise.
 *
 * 100000 doesn't fit a 16-bit immediate, so javac stores it in the
 * constant pool (a CONSTANT_Integer) and loads it with `ldc`. Run
 * `javap -v IntPatchTestApp.class` to see it.
 */
public class IntPatchTestApp {

    static int dailyLimit() {
        return 100000;
    }

    public static void main(String[] args) {
        System.out.println("Daily limit: " + dailyLimit());
    }
}
