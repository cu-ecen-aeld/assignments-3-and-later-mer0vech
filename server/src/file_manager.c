#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#include "file_manager.h"
#include "config_manager.h"
#include "common.h"
#include "server_utils.h"


void*
timestamp_worker(void *arg)
{
  (void)arg;

  while(keep_running) {
    for(int i = 0; i < 10 && keep_running; i++) {
      sleep(1);
    }
    
    if(!keep_running) break;

    struct tm timeinfo;
    char buffer[80];
    time_t rawtime = time(NULL);
    localtime_r(&rawtime, &timeinfo);

    strftime(buffer, sizeof(buffer), "timestamp:%Y-%m-%d %H:%M:%S", &timeinfo);

    pthread_mutex_lock(&file_mutex);
    append_line_to_file(buffer);
    pthread_mutex_unlock(&file_mutex);

    syslog(LOG_DEBUG, "timestamp written to file");
  }
  return NULL;
}

void 
append_line_to_file(const char *data)
{
  if(global_log_fd == -1) {
    syslog(LOG_ERR, "log file descriptor is not initialized");
    return;
  }

  if(write(global_log_fd, data, strlen(data)) < 0) {
    syslog(LOG_ERR, "error writing to file");
  }
  
  if(write(global_log_fd, "\n", 1) < 0) {
    syslog(LOG_ERR, "error writing to file");
  }

}

char*
read_file_to_buffer(size_t *out_size)
{
  if(global_log_fd == -1) {
    syslog(LOG_ERR, "log file descriptor is not initialized");
    return NULL;
  }

  struct stat st;
  if(fstat(global_log_fd, &st) == -1) {
    syslog(LOG_ERR, "Failed to stat %s", server_cfg.log_file);
    return NULL;
  }
  
  syslog(LOG_INFO, "fstat: %ld bytes", st.st_size);

  *out_size = st.st_size;
  char *content = safe_calloc(1, *out_size + 1);

  ssize_t bytes_read = pread(global_log_fd, content, *out_size, 0);
  if(bytes_read != (ssize_t)*out_size) {
    syslog(LOG_ERR, "File read error (expected %zu bytes, got %zd bytes)", *out_size, bytes_read);
    free(content);
    content = NULL;
  } else {
    content[*out_size] = '\0';
  }

  return content;
}
