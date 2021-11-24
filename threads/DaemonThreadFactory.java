import java.util.concurrent.ThreadFactory;

public class DaemonThreadFactory implements ThreadFactory {

	@Override
	public Thread newThread(Runnable command) {
		return new DaemonThread("Daemon", command);
	}

}
