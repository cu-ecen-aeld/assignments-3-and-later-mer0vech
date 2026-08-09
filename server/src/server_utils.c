#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <sys/queue.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>

#include "thread_pool.h"
#include "config_manager.h"
#include "server_utils.h"
#include "file_manager.h"
#include "common.h"
#include "../../aesd-char-driver/aesd_ioctl.h"

#define START_BUFFER_SIZE 1024
#define MAX_RAM_PER_CLIENT (10 * 1024 * 1024)


// --- GLOBALS --- 

volatile sig_atomic_t keep_running = 1;
volatile sig_atomic_t last_sig = 0;

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

#ifndef USE_AESD_CHAR_DEVICE
int global_log_fd = -1;
#endif

static
char*
exec_driver_ioctl(uint32_t cmd, uint32_t offset, size_t *out_size)
{
  struct aesd_seekto seek_data = { .write_cmd = cmd, .write_cmd_offset = offset };
  size_t max_capacity = 64 * 1024;
  char *content = safe_malloc(max_capacity);
  size_t total_bytes_read = 0;

  int drv_fd = open(server_cfg.log_file, O_RDWR);
  if(drv_fd < 0) {
    syslog(LOG_ERR, "Unable to open config file :%m");
    free(content);
    return NULL;
  }

  if(ioctl(drv_fd, AESDCHAR_IOCSEEKTO, &seek_data) == 0) {
    syslog(LOG_INFO, "IOCTL seekto successful, reading...");
    
    ssize_t bytes_read;
    while ((bytes_read = read(drv_fd, content + total_bytes_read, max_capacity - total_bytes_read - 1)) > 0) {
      total_bytes_read += bytes_read;
      if (total_bytes_read >= max_capacity - 1) {
        break;
      }
    }

    if (total_bytes_read > 0) {
      content[total_bytes_read] = '\0';
      *out_size = total_bytes_read;
    } else {
      syslog(LOG_WARNING, "Driver returned 0 bytes after IOCTL!");
      free(content);
      content = NULL;
      *out_size = 0;
    }
  } else {
    syslog(LOG_ERR, "IOCTL command failed :%m");
    free(content);
    content = NULL;
  }

  close(drv_fd);
  return content;
}


// --- FILE HANDLERS ---
int 
init_log_file(const char *path)
{
  #ifndef USE_AESD_CHAR_DEVICE
  global_log_fd = open(path, O_RDWR | O_APPEND | O_CREAT, 0644);
  if(global_log_fd == -1) {
    syslog(LOG_ERR, "unable to open log file (%s) for r/w operation: %m", path);
    return 1;
  }
  #else
  (void)path;
  #endif

  return 0;
}

void 
close_log_file(void)
{
  #ifndef USE_AESD_CHAR_DEVICE
  if(global_log_fd != -1) {
    close(global_log_fd);
    global_log_fd = -1;
    syslog(LOG_INFO, "log file closed successfully");
  }
  #endif
}

// --- NET FUNCTIONS ---

int 
listen_on_port(const char *port)
{
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int yes = 1;
  int rv;

  memset(&hints, 0, sizeof hints);

  hints.ai_family = AF_INET; // IPv4
  hints.ai_socktype = SOCK_STREAM; // TCP
  hints.ai_flags = AI_PASSIVE; // Use host IP

  if((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
    syslog(LOG_ERR, "Getaddrinfo error: %s", gai_strerror(rv));
    return -1;
  }

  // Bind to the first available address
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      continue;
    }
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      syslog(LOG_ERR, "Setsockopt error: %m");
      return -1;
    }
    if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      continue;
    }
    break;
  }

  freeaddrinfo(servinfo);
  
  if(p == NULL) {
    syslog(LOG_ERR, "Server failed to bind!");
    return -1;
  }
  if(listen(sockfd, BACKLOG) == -1) {
    syslog(LOG_ERR, "Server listen error: %m");
    return -1;
  }

  return sockfd;

}

void*
get_in_addr(struct sockaddr *sa)
{
  if(sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

void 
handle_client(int client_fd)
{
  size_t buffer_size = START_BUFFER_SIZE;
  char *buffer = safe_malloc(buffer_size);

  size_t pos = 0;
  char temp_char;
  ssize_t n;

  struct timeval timeout;
  timeout.tv_sec = 30;
  timeout.tv_usec = 0;

  setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  syslog(LOG_INFO, "Client %d connected", client_fd);

  while((n = recv(client_fd, &temp_char, 1, 0)) > 0) {
    
    if(pos + 2 >= buffer_size) {
      if(buffer_size * 2 > MAX_RAM_PER_CLIENT) {
        syslog(LOG_WARNING, "RAM limit for client %d exceeded", client_fd);
        break;
      }
      buffer_size *= 2;
      buffer = safe_realloc(buffer, buffer_size);
    }

    if(temp_char != '\r') {
      buffer[pos++] = temp_char;
    }

    if(temp_char == '\n') {
      buffer[pos] = '\0'; 

      char *file_content = NULL;
      size_t content_size = 0;

      if(pos >= 19 && strncmp(buffer, "AESDCHAR_IOCSEEKTO:", 19) == 0) {
        uint32_t cmd;
        uint32_t offset;

        if(sscanf(buffer + 19, "%u,%u", &cmd, &offset) == 2) {
          syslog(LOG_INFO, "Caught IOCTL request - CMD: %u, OFFSET: %u", cmd, offset);
          pthread_mutex_lock(&file_mutex);
          file_content = exec_driver_ioctl(cmd, offset, &content_size);
          pthread_mutex_unlock(&file_mutex);
        }
      } else {
        pthread_mutex_lock(&file_mutex);
        append_line_to_file(buffer);
        syslog(LOG_INFO, "line appended to file");
        file_content = read_file_to_buffer(&content_size);
        pthread_mutex_unlock(&file_mutex);
      }

      if (file_content != NULL) {
        send(client_fd, file_content, content_size, 0);
        free(file_content);
      } else {
        send(client_fd, "", 0, 0);
      }
      
      pos = 0;
    } 
  }
	
  if(pos > 0) {
    buffer[pos] = '\0'; 
    pthread_mutex_lock(&file_mutex);
    append_line_to_file(buffer);
    pthread_mutex_unlock(&file_mutex);
    syslog(LOG_INFO, "Final partial line appended before closing");
  }
  
  if(n < 0) {
    if(errno == EAGAIN || errno == EWOULDBLOCK) {
      syslog(LOG_WARNING, "Client %d timed-out", client_fd);
    } else {
      syslog(LOG_ERR, "Client %d socket error: %m", client_fd);
    }
  }

  free(buffer);
  close(client_fd);
  syslog(LOG_INFO, "Connection for client %d closed", client_fd);
}


// --- SYS FUNCTIONS ---

int 
daemonize()
{
  pid_t pid;

  pid = fork();
  if(pid < 0) return -1; // Error
  if(pid > 0) exit(0); // Child continues

  if(setsid() < 0) return -1; // Session failed

  // Second for to lock out the daemon
  pid = fork();
  if(pid < 0) return -1; // Error
  if(pid > 0) exit(0); // Child continues

  umask(0);
  if(chdir("/") < 0) {
    syslog(LOG_ERR, "unamble to chdir to root");
    return -1;
  }

  int fd = open("/dev/null", O_RDWR);
  if (fd != -1) {
    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    
    if (fd > 2) {
      close(fd);
    }
  }

  return 0;
}

int 
setup_signal_handlers()
{
  struct sigaction sa;

  sa.sa_handler = stop_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  if(sigaction(SIGTERM, &sa, NULL) == -1) {
    syslog(LOG_ERR, "sigterm error: %m");
    return -1;
  }

  if(sigaction(SIGINT, &sa, NULL) == -1) {
    syslog(LOG_ERR, "sigint error: %m");
    return -1;
  }

  signal(SIGPIPE, SIG_IGN);
  return 0;
}

void 
stop_handler(int s)
{
  last_sig = s;

  keep_running = 0;
}

// --- MAIN LOGIC ---

int 
run_server(int sock_fd)
{
  if(init_log_file(server_cfg.log_file) != 0) return 1;

  syslog(LOG_INFO, "Waiting for connections on port %s", server_cfg.port);

  // Main loop
  struct sockaddr_storage their_addr; // Address info of connectee
  socklen_t sin_size;
  char s[INET6_ADDRSTRLEN];
  int new_fd;

  while(keep_running) {
    sin_size = sizeof their_addr;

    // Get new connection
    new_fd = accept(sock_fd, (struct sockaddr *)&their_addr, &sin_size);
    if(new_fd == -1) {
      if(errno == EINTR) continue;
      syslog(LOG_ERR, "Accept error: %m");
      continue;
    }

    // Logging connection
    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
    syslog(LOG_INFO, "Accepted connection from %s", s);

    thread_pool_enqueue(new_fd);
  }

  syslog(LOG_INFO, "Server exiting...");
  close_log_file();

  return 0;
}
