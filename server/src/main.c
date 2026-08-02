#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <pthread.h>

#include "server_utils.h"
#include "config_manager.h"
#include "thread_pool.h"
#include "file_manager.h"

int main(int argc, char *argv[])
{
  load_config("server.conf");

  #ifndef USE_AESD_CHAR_DEVICE
  remove(server_cfg.log_file);
  #endif

  int do_daemon = 0;

  // Process arguments
  if(argc > 2) {
    fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
    return 1;
  }

  if(argc == 2) {
    if(strcmp(argv[1], "-d") == 0) {
      do_daemon = 1;
    } else {
      fprintf(stderr, "Unknown argument: %s\n", argv[1]);
      return 1;
    }
  }

  // Daemonize if required
  if(do_daemon) {
    if(daemonize() == -1) {
      fprintf(stderr, "Daemonization failed!\n");
      return 1;
    }
  }

  // Open log
  openlog("aesdsocket", LOG_PID, LOG_DAEMON);
  syslog(LOG_INFO, "Running server...");

  // Setup sighadlers
  if(setup_signal_handlers() != 0) {
    syslog(LOG_ERR, "Unable to setup sighandlers");
    return 1;
  }

  // Init thread-pool
  init_thread_pool(server_cfg.max_threads);

  // Socket setup
  int sock_fd = listen_on_port(server_cfg.port);
  if(sock_fd == -1) {
    syslog(LOG_ERR, "server cannot connect on port %s", server_cfg.port);
    thread_pool_cleanup();
    return 1;
  }

  #ifndef USE_AESD_CHAR_DEVICE
  // Set up timestamp
  pthread_t timestamp_tid;
  if(pthread_create(&timestamp_tid, NULL, timestamp_worker, NULL) != 0) {
    syslog(LOG_ERR, "unable to run timestamp thread");
    return 1;
  } else {
    syslog(LOG_INFO, "timestamp running every 10 seconds");
  }
  #endif

  // Run main logic
  int status = run_server(sock_fd);

  // Clean and exit
  keep_running = 0;
  syslog(LOG_INFO, "shutting down with status %d", status);
  
  thread_pool_cleanup();

  #ifndef USE_AESD_CHAR_DEVICE
  pthread_join(timestamp_tid, NULL);
  #endif
  
  close(sock_fd);
  closelog();

  return status;
}
