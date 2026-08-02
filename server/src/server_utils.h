#pragma once

#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define BACKLOG 10


// --- GLOBALS ---

extern volatile sig_atomic_t keep_running;
extern volatile sig_atomic_t last_sig;
extern pthread_mutex_t file_mutex;

#ifndef USE_AESD_CHAR_DEVICE
extern int global_log_fd;
#endif

// --- FILE HANDLERS ---
int init_log_file(const char *path);
void close_log_file(void);

// --- NET FUNCTIONS ---

/*
* Listens for connections on given port
* args: port address (const char *)
* returns: socket file descriptor (int)
*/
int listen_on_port(const char *port);

/*
* Get sock address (IPv4 or IPv6)
* args: pointer to sock address structure (struct *)
*/
void *get_in_addr(struct sockaddr *sa);

/*
* Handles connected client
* args: client file descriptor (int)
*/
void handle_client(int client_fd);

// --- SYS FUNCTIONS ---

/*
* Daemonizes the server
*/
int daemonize();

/*
* Signal handlers
*/
int setup_signal_handlers();
void stop_handler(int s);

// --- MAIN LOGIC ---

/*
* Runs the server
*/
int run_server();
