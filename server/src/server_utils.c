#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>

#include "server_utils.h"
#include "file_manager.h"

#define START_BUFFER_SIZE 1024
#define MAX_RAM_PER_CLIENT (10 * 1024 * 1024)

// --- GLOBALS --- 

volatile sig_atomic_t keep_running = 1;
volatile sig_atomic_t last_sig = 0;

// --- NET FUNCTIONS ---

int listen_on_port(const char *port)
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

void *get_in_addr(struct sockaddr *sa)
{
  if(sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

void handle_client(int client_fd)
{
  size_t buffer_size = START_BUFFER_SIZE;
  char *buffer = malloc(buffer_size);
  if(buffer == NULL) {
    syslog(LOG_ERR, "Malloc error for client %d", client_fd);
    close(client_fd);
    return;
  }

  size_t pos = 0;
  char temp_char;
  ssize_t n;

  syslog(LOG_INFO, "Client %d connected", client_fd);

  while((n = recv(client_fd, &temp_char, 1, 0)) > 0) {
    if(temp_char == '\n') {
      buffer[pos] = '\0';
      if(pos > 0) {
        append_line_to_file(buffer);
        syslog(LOG_INFO, "line appended to file");
      }
      size_t file_size = 0;
      char *file_data = read_file_to_buffer(&file_size);
      if(file_data) {
        send(client_fd, file_data, file_size, 0);
        free(file_data);
        syslog(LOG_INFO, "sent %zu bytes to client %d", file_size, client_fd);
      } else {
        syslog(LOG_INFO, "file empty");
      }

      pos = 0;
      break;

    } else {
      if(pos + 2 >= buffer_size) {
        if(buffer_size * 2 > MAX_RAM_PER_CLIENT) {
          syslog(LOG_WARNING, "RAM limit for client %d exceeded", client_fd);
          break;
        }
        buffer_size *= 2;
        char *new_buffer = realloc(buffer, buffer_size);
        if(new_buffer == NULL) {
          syslog(LOG_ERR, "Realloc failed");
          break;
        }
        buffer = new_buffer;
      }
      if(temp_char != '\r') {
        buffer[pos++] = temp_char;
      }
    }

    if(n < 0) {
      syslog(LOG_ERR, "Error reading socket for client %d", client_fd);
    }
    

  }

  free(buffer);
  close(client_fd);
  syslog(LOG_INFO, "Connection for client %d closed", client_fd);
}


// --- SYS FUNCTIONS ---

int daemonize()
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

  // No need for std file descriptors
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  // Open /dev/null for std
  open("/dev/null", O_RDONLY); 
  open("/dev/null", O_WRONLY); 
  open("/dev/null", O_WRONLY); 

  return 0;
}

int setup_signal_handlers()
{
  struct sigaction sa;

  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if(sigaction(SIGCHLD, &sa, NULL) == -1) {
    syslog(LOG_ERR, "sigchld error: %m");
    return -1;
  }

  sa.sa_handler = sigterm_handler;
  sa.sa_flags = 0;
  if(sigaction(SIGTERM, &sa, NULL) == -1) {
    syslog(LOG_ERR, "sigterm error: %m");
    return -1;
  }

  if(sigaction(SIGINT, &sa, NULL) == -1) {
    syslog(LOG_ERR, "sigint error: %m");
    return -1;
  }

  return 0;
}

void sigchld_handler(int s)
{
  (void)s; // Variable warning gag

  // Save errno in case of overwrite
  int saved_errno = errno;

  while(waitpid(-1, NULL, WNOHANG) > 0);

  errno = saved_errno;
}

void sigterm_handler(int s)
{
  last_sig = s;

  keep_running = 0;
}

// --- MAIN LOGIC ---

int run_server()
{
  // Socket/Bind/Listen init
  int sockfd;
  sockfd = listen_on_port(PORT);
  if(sockfd == -1) return 1;

  // Sig setup
  if(setup_signal_handlers() == -1) {
    close(sockfd);
    return 1;
  }

  syslog(LOG_INFO, "Waiting for connections on port %s", PORT);

  // Main loop
  struct sockaddr_storage their_addr; // Address info of connectee
  socklen_t sin_size;
  char s[INET6_ADDRSTRLEN];
  int new_fd;

  while(keep_running) {
    sin_size = sizeof their_addr;

    // Get new connection
    new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
    if(new_fd == -1) {
      if(errno == EINTR) continue;
      syslog(LOG_ERR, "Accept error: %m");
      continue;
    }

    // Logging connection
    inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
    syslog(LOG_INFO, "Accepted connection from %s", s);

    pid_t pid = fork();

    if(pid < 0) {
      syslog(LOG_ERR, "Fork error: %m");
      close(new_fd);
      continue;
    }
    if(pid == 0) {
      close(sockfd);
      handle_client(new_fd);
      exit(0);
    }

    close(new_fd);
  }

  // Cleanup and exit
  if(last_sig == SIGINT || last_sig == SIGTERM) {
    syslog(LOG_INFO, "Caught signal, exiting");
  } else if (keep_running == 0) {
    syslog(LOG_INFO, "exiting normally");
  }
  close(sockfd);

  return 0;
}
