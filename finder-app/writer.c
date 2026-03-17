#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <syslog.h>
#include <string.h>
#include <fcntl.h>
#include <libgen.h>


int main(int argc, char* argv[])
{

  openlog("aeld_writer", LOG_PID, LOG_USER);
  syslog(LOG_INFO, "Program started by user %s", getenv("USER"));

  // Verify argument number is 2
  if(argc != 3) {
    printf("Error: Invalid number of parameters given.\n");
    printf("Usage: %s <path> <text_string>\n", argv[0]);
    syslog(LOG_ERR, "Invalid number of parameters given.");
    return EXIT_FAILURE;
  }

  // Get arguments
  const char* path = argv[1];
  const char* text = argv[2];

  // Verify path is valid and text string is not empty
  char path_temp[sizeof(path)];
  strncpy(path_temp, path, sizeof(path_temp));
  char* directory_path = dirname(path_temp);
  struct stat stats;
  if((stat(directory_path, &stats) != 0) || (!S_ISDIR(stats.st_mode))) {
    perror("Error: Invalid path.\n");
    syslog(LOG_ERR, "Invalid path given.");
    return EXIT_FAILURE;
  }
  if(strlen(text) == 0) {
    perror("Error: Text string cannot be empty.\n");
    syslog(LOG_ERR, "Empty string given.");
    return EXIT_FAILURE;
  }

  // Open file for writing
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
  if(fd == -1) {
    perror("Error: Unable to open file.\n");
    syslog(LOG_ERR, "Unable to open file: %m");
    return EXIT_FAILURE;
  }
  
  // Write the text string to file
  syslog(LOG_DEBUG, "Writing '%s' to file '%s'", text, path);
  ssize_t write_ret = write(fd, text, strlen(text));
  if(write_ret == -1) {
    perror("Error: Unable to write to file.\n");
    syslog(LOG_ERR, "Unable to write to file: %m");
    return EXIT_FAILURE;
  }

  // Close the file
  if(close(fd) == -1){
    perror("Error: Unable to close the file.\n");
    syslog(LOG_ERR, "Unable to close file: %m");
    return EXIT_FAILURE;
  }

  closelog();

  return EXIT_SUCCESS;

}
