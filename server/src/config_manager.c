#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "config_manager.h"

Config server_cfg;

void 
load_config(const char *filename)
{
  FILE *file = fopen(filename, "r");

  if(!file) {
    syslog(LOG_DEBUG, "Configuration file not found. Using defaults...");
    strncpy(server_cfg.port, "9000", sizeof(server_cfg.port) - 1);
    server_cfg.port[sizeof(server_cfg.port) - 1] = '\0';
    server_cfg.max_threads = 4;
    server_cfg.queue_size = 10;
    #ifdef USE_AESD_CHAR_DEVICE
    strncpy(server_cfg.log_file, "/dev/aesdchar", sizeof(server_cfg.log_file) - 1);
    #else
    strncpy(server_cfg.log_file, "/var/tmp/aesdsocketdata", sizeof(server_cfg.log_file) - 1);
    #endif
    server_cfg.log_file[sizeof(server_cfg.log_file) - 1] = '\0';
    return;
  }

  char line[512];
  while(fgets(line, sizeof(line), file)) {
    if(line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

    char key[64], value[256];

    if(sscanf(line, " %63[^=] = %255[^\n\r]", key, value) == 2) {
      char *end = key + strlen(key) - 1;
      while(end > key && (*end == ' ' || *end == '\t')) {
        *end = '\0';
        end--;
      }

      if(strcmp(key, "PORT") == 0) {
        strncpy(server_cfg.port, value, sizeof(server_cfg.port) - 1);
        server_cfg.port[sizeof(server_cfg.port) - 1] = '\0';
      } else if(strcmp(key, "MAX_THREADS") == 0) {
        int val = atoi(value);
        if(val > 0 && val <= 100) {
          server_cfg.max_threads = val;
        } else {
          syslog(LOG_WARNING, "Invalid MAX_THREADS given (%d), using default (4)", val);
          server_cfg.max_threads = 4;
        }
      } else if(strcmp(key, "QUEUE_SIZE") == 0) {
        int val = atoi(value);
        if(val > 0 && val <= 1000) {
          server_cfg.queue_size = val;
        } else {
          syslog(LOG_WARNING, "Invalid QUEUE_SIZE given (%d), using default (10)", val);
          server_cfg.queue_size = 10;
        }
      } else if(strcmp(key, "LOG_FILE") == 0) {
        #ifdef USE_AESD_CHAR_DEVICE
        strncpy(server_cfg.log_file, "/dev/aesdchar", sizeof(server_cfg.log_file) - 1);
        #else
        strncpy(server_cfg.log_file, value, sizeof(server_cfg.log_file) - 1);
        #endif
        server_cfg.log_file[sizeof(server_cfg.log_file) - 1] = '\0';
      }
    }
  }

  fclose(file);
  syslog(LOG_INFO, "Configuration read: Port=%s, Threads=%d, Queue=%d, Log file path=%s", 
         server_cfg.port, server_cfg.max_threads, server_cfg.queue_size, server_cfg.log_file);

}
