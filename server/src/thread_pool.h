#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <stdbool.h>

// Initializes thread pool
void init_thread_pool(int num_threads);

// Adds new client to the pool
bool thread_pool_enqueue(int client_fd);

// Dinamically adjust the number of threads
void adjust_thread_pool(int new_count);

// Thread pool clean-up
void thread_pool_cleanup();


#endif


