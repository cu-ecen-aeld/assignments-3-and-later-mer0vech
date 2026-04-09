#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "server_utils.h"

int main(int argc, char *argv[])
{
  remove("/var/tmp/aesdsocketdata");

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

  // Run main logic
  int status = run_server();

  // Clean and exit
  syslog(LOG_INFO, "shutting down with status %d", status);
  closelog();

  return status;
}
