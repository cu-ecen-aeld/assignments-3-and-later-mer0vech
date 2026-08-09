#define _GNU_SOURCE

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>

#include "file_manager.h"
#include "config_manager.h"
#include "common.h"
#include "server_utils.h"

#ifndef USE_AESD_CHAR_DEVICE
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
#endif

void 
append_line_to_file(const char *data)
{
  if(data == NULL) {
    syslog(LOG_ERR, "append_line_to_file received NULL");
    return;
  }

  #ifdef USE_AESD_CHAR_DEVICE
  int fd = open(server_cfg.log_file, O_WRONLY | O_CREAT, 0666);

  if(fd < 0) {
    syslog(LOG_ERR, "log file descriptor is not initialized");
    return;
  }

  size_t len = strlen(data);
  if(len == 0) {
    close(fd);
    return;
  }

  char *out;

  if(data[len - 1] == '\n') {
    if(write(fd, data, len) < 0) {
      syslog(LOG_ERR, "error writing to file");
    }
  } else {
    if(asprintf(&out, "%s\n", data) == -1) {
      syslog(LOG_ERR, "error parsing write string for driver");
      close(fd);
      return;
    }
    if(write(fd, out, strlen(out)) < 0) {
      syslog(LOG_ERR, "error writing to file");
    }
    free(out);
  }

  close(fd);

  #else

  if(global_log_fd == -1) {
    syslog(LOG_ERR, "log file descriptor is not initialized");
    return;
  }

  char *out;
  if(asprintf(&out, "%s\n", data) == -1) {
    syslog(LOG_ERR, "error parsing write string");
    return;
  }

  if(write(global_log_fd, out, strlen(out)) < 0) {
    syslog(LOG_ERR, "error writing to file");
  }

  fsync(global_log_fd);
  free(out);

  #endif
}

char*
read_file_to_buffer(size_t *out_size)
{
  if(out_size == NULL) return NULL;

  #ifdef USE_AESD_CHAR_DEVICE
  int fd = open(server_cfg.log_file, O_RDONLY);
  if(fd < 0) {
    syslog(LOG_ERR, "log file descriptor is not initialized");
    *out_size = 0;
    return NULL;
  }

  size_t capacity = 64 * 1024; 
  char *content = safe_malloc(capacity);
  size_t total_read = 0;
  ssize_t bytes_read;

  while ((bytes_read = read(fd, content + total_read, capacity - total_read - 1)) > 0) {
    total_read += bytes_read;
    if (total_read >= capacity - 1) {
      break;
    }
  }

  close(fd);

  if(bytes_read < 0) {
    syslog(LOG_ERR, "error reading from char device :%m");
    free(content);
    *out_size = 0;
    return NULL;
  }

  if(total_read == 0) {
    free(content);
    *out_size = 0;
    return NULL;
  }

  content[total_read] = '\0';
  *out_size = total_read;

  return content;

  #else

  if(global_log_fd == -1) {
    syslog(LOG_ERR, "log file descriptor is not initialized");
    *out_size = 0;
    return;
  }

  off_t current_pos = lseek(global_log_fd, 0, SEEK_END);
  if(current_pos <= 0) {
    *out_size = 0;
    return NULL;
  }

  *out_size = (size_t)current_pos;
  char *content = safe_calloc(1, *out_size + 1);

  ssize_t bytes_read = pread(global_log_fd, content, *out_size, 0);

  if(bytes_read != (ssize_t)*out_size) {
    syslog(LOG_ERR, "File read error (expected %zu bytes, got %zd bytes)", *out_size, bytes_read);
    free(content);
    content = NULL;
    *out_size = 0;
  } else {
    content[*out_size] = '\0';
  }

  return content;

  #endif

}
