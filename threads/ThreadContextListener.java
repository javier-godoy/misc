import javax.servlet.ServletContextEvent;
import javax.servlet.ServletContextListener;

public class ThreadContextListener implements ServletContextListener {

	@Override
	public void contextInitialized(ServletContextEvent sce) {
		return;
	}

	@Override
	public void contextDestroyed(ServletContextEvent sce) {
		DaemonThreads.shutdown();
	}

}
