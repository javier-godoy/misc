import java.util.concurrent.BlockingDeque;
import java.util.concurrent.LinkedBlockingDeque;


public class DaemonThreads {

	private static final BlockingDeque<DaemonThread> threads = new LinkedBlockingDeque<>();

	private DaemonThreads() {}

	static void registerThread(DaemonThread t) {
		threads.add(t);
	}

	public static void shutdown() {
		while (true) {
			DaemonThread t = threads.poll();
			if (t==null) break;
			t.interrupt();
		}
	}

}
