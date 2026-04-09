#pragma once

#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define PORT "9000"
#define BACKLOG 10


// --- GLOBALS ---

extern volatile sig_atomic_t keep_running;
extern volatile sig_atomic_t last_sig;

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
void sigchld_handler(int s);
void sigterm_handler(int s);

// --- MAIN LOGIC ---

/*
* Runs the server
*/
int run_server();
