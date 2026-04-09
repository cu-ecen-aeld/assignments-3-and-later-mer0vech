#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include "file_manager.h"

void append_line_to_file(const char *data)
{
  int fd = open(IO_FILE, O_WRONLY | O_APPEND | O_CREAT, 0644);
  if(fd == -1) {
    syslog(LOG_ERR, "Unable to open file for writing");
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

char *read_file_to_buffer(size_t *out_size)
{
  int fd = open(IO_FILE, O_RDONLY);
  if(fd == -1) {
    syslog(LOG_ERR, "Unable to open file for reading");
    return NULL;
  }

  struct stat st;
  if(fstat(fd, &st) == -1) {
    syslog(LOG_ERR, "Failed to stat %s", IO_FILE);
    close(fd);
    return NULL;
  }
  
  syslog(LOG_INFO, "fstat: %ld bytes", st.st_size);

  *out_size = st.st_size;
  char *content = malloc(*out_size + 8);
  if(content == NULL) {
    syslog(LOG_ERR, "Failed to malloc");
    close(fd);
    return NULL;
  }

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
