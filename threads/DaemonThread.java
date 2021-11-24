public class DaemonThread extends Thread {

	public DaemonThread(Runnable target) {
		super(target);
		this.setDaemon(true);
		DaemonThreads.registerThread(this);
	}

	public DaemonThread(String name, Runnable target) {
		super(target);
		this.setName(name + "-" + this.getId());
		this.setDaemon(true);
		DaemonThreads.registerThread(this);
	}

}
