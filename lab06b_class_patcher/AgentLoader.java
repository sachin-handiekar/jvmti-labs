import com.sun.tools.attach.VirtualMachine;

/**
 * AgentLoader.java — loads a native agent into a RUNNING JVM (§11.4).
 *
 * The Attach API equivalent of `jcmd <pid> JVMTI.agent_load`: the
 * target JVM calls the agent's Agent_OnAttach instead of Agent_OnLoad.
 *
 * Usage:  java AgentLoader <pid> </abs/path/to/agent-library> [options]
 */
public class AgentLoader {
    public static void main(String[] args) throws Exception {
        if (args.length < 2) {
            System.err.println("usage: java AgentLoader <pid> <agent-path> [options]");
            System.exit(2);
        }
        String pid = args[0];
        String agentPath = args[1];
        String agentArgs = args.length > 2 ? args[2] : "";

        VirtualMachine vm = VirtualMachine.attach(pid);
        try {
            vm.loadAgentPath(agentPath, agentArgs);
            System.out.println("Agent loaded into PID " + pid);
        } finally {
            vm.detach();
        }
    }
}
