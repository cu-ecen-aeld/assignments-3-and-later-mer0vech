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
  int fd = open(server_cfg.log_file, O_WRONLY | O_APPEND | O_CREAT, 0644);
  if(fd == -1) {
    syslog(LOG_ERR, "Unable to open file (%s) for writing: %m", server_cfg.log_file);
    return;
  }

  if(write(fd, data, strlen(data)) < 0) {
    syslog(LOG_ERR, "error writing to file");
  }
  
  if(write(fd, "\n", 1) < 0) {
    syslog(LOG_ERR, "error writing to file");
  }

  fsync(fd);
  close(fd);
}

char*
read_file_to_buffer(size_t *out_size)
{
  int fd = open(server_cfg.log_file, O_RDONLY);
  if(fd == -1) {
    syslog(LOG_ERR, "Unable to open file (%s) for reading: %m", server_cfg.log_file);
    return NULL;
  }

  struct stat st;
  if(fstat(fd, &st) == -1) {
    syslog(LOG_ERR, "Failed to stat %s", server_cfg.log_file);
    close(fd);
    return NULL;
  }
  
  syslog(LOG_INFO, "fstat: %ld bytes", st.st_size);

  *out_size = st.st_size;
  char *content = safe_calloc(1, *out_size + 1);

  ssize_t bytes_read = read(fd, content, *out_size);
  if(bytes_read != (ssize_t)*out_size) {
    syslog(LOG_ERR, "File read error (expected %zu bytes, got %zd bytes)", *out_size, bytes_read);
    free(content);
    content = NULL;
  } else {
    content[*out_size] = '\0';
  }

  close(fd);

  return content;
}
