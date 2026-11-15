import java.io.*;
import java.lang.instrument.Instrumentation;
import java.util.Map;

public class DumpAgent {
    public static void premain(String args, Instrumentation inst) {
        final String path = (args == null || args.isEmpty()) ? "threads.txt" : args;
        Thread t = new Thread(() -> {
            int n = 0;
            while (true) {
                try { Thread.sleep(20000); } catch (InterruptedException e) { return; }
                try { StringWriter sw = new StringWriter(); PrintWriter w = new PrintWriter(sw);
                    w.println("PCPS4DUMP " + (++n) + " at " + System.currentTimeMillis());
                    try {
                        java.lang.management.CompilationMXBean c = java.lang.management.ManagementFactory.getCompilationMXBean();
                        java.lang.management.RuntimeMXBean r = java.lang.management.ManagementFactory.getRuntimeMXBean();
                        w.println("JIT " + (c == null ? "none" : c.getName() + " compileMs=" + c.getTotalCompilationTime())
                                  + " uptimeMs=" + r.getUptime() + " vm=" + System.getProperty("java.vm.info")
                                  + " args=" + r.getInputArguments().stream().filter(a -> a.startsWith("-X")).toList());
                        java.lang.management.ThreadMXBean tb = java.lang.management.ManagementFactory.getThreadMXBean();
                        w.println("CPU main=" + (tb.isThreadCpuTimeSupported() ? tb.getThreadCpuTime(1) / 1000000 + "ms" : "n/a")
                                  + " classes=" + java.lang.management.ManagementFactory.getClassLoadingMXBean().getLoadedClassCount());
                    } catch (Throwable e) { w.println("JIT info failed: " + e); }
                    for (Map.Entry<Thread, StackTraceElement[]> e : Thread.getAllStackTraces().entrySet()) {
                        Thread th = e.getKey();
                        w.println("\"" + th.getName() + "\" " + th.getState() + (th.isDaemon() ? " daemon" : ""));
                        for (StackTraceElement s : e.getValue()) w.println("    at " + s);
                        w.println();
                    }
                    w.flush(); for (String line : sw.toString().split("\n")) System.err.println("PCPS4DUMP| " + line); System.err.flush();
                } catch (Throwable ignored) { }
            }
        }, "pcps4-thread-dumper");
        t.setDaemon(true);
        t.start();
    }
}
