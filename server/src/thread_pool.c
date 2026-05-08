#include <syslog.h>
#include <stdlib.h>
#include <unistd.h>

#include "thread_pool.h"
#include "config_manager.h"
#include "server_utils.h"
#include "common.h"


static pthread_t *threads = NULL;
static int current_pool_size = 0;

static int *task_queue = NULL;
static int queue_count = 0;
static int queue_head = 0;
static int queue_tail = 0;


static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;


static void*
thread_worker(void *arg)
{
  (void)arg;
  while(1) {
    int client_fd = -1;

    pthread_mutex_lock(&queue_mutex);

    while(queue_count == 0 && keep_running) {
      pthread_cond_wait(&queue_cond, &queue_mutex);
    }

    if(!keep_running && queue_count == 0) {
      pthread_mutex_unlock(&queue_mutex);
      break;
    }

    client_fd = task_queue[queue_head];
    queue_head = (queue_head + 1) % server_cfg.queue_size;
    queue_count--;

    pthread_mutex_unlock(&queue_mutex);

    if(client_fd != -1) {
      handle_client(client_fd);
    }
  }

  return NULL;
}

void
init_thread_pool(int num_threads)
{

  current_pool_size = num_threads;
  threads = safe_malloc(sizeof(pthread_t) * current_pool_size);
  task_queue = safe_malloc(sizeof(int) * server_cfg.queue_size);
  
  pthread_mutex_lock(&queue_mutex);
  queue_count = 0;

  for(int i = 0; i < current_pool_size; i++) {
    pthread_create(&threads[i], NULL, thread_worker, NULL);
  }

  pthread_mutex_unlock(&queue_mutex);

  syslog(LOG_INFO, "Thread-pool initialized with %d threads.", num_threads);
}

bool
thread_pool_enqueue(int client_fd)
{
  bool enqueued = false;
  pthread_mutex_lock(&queue_mutex);

  if(queue_count < server_cfg.queue_size) {
    task_queue[queue_tail] = client_fd;
    queue_tail = (queue_tail + 1) % server_cfg.queue_size;
    queue_count++;
    pthread_cond_signal(&queue_cond);
    enqueued = true;
  } else {
    syslog(LOG_WARNING, "Queue is full (size: %d); dropping connection for socket: %d", server_cfg.queue_size, client_fd);
    close(client_fd);
    enqueued = false;
  }

  pthread_mutex_unlock(&queue_mutex);
  return enqueued;
}

void
adjust_thread_pool(int new_count)
{
  if(new_count <= 0) return;
  
  pthread_mutex_lock(&queue_mutex);

  if(new_count > current_pool_size) {
    threads = safe_realloc(threads, sizeof(pthread_t) * new_count);

    for(int i = current_pool_size; i < new_count; i++) {
      if(pthread_create(&threads[i], NULL, thread_worker, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create new thread at index %d", i);
      }
    }
    syslog(LOG_INFO, "Pool enlarged to %d threads", new_count);
  } else if(new_count < current_pool_size) {
    int diff = current_pool_size - new_count;
    for(int i = 0; i < diff; i++) {
      if(queue_count < server_cfg.queue_size) {
        task_queue[queue_tail] = -1;
        queue_tail = (queue_tail + 1) % server_cfg.queue_size;
        queue_count++;
        pthread_cond_signal(&queue_cond);
      }
    }
    syslog(LOG_INFO, "Sent %d pills for pool reduction", diff);
  }

  current_pool_size = new_count;
  pthread_mutex_unlock(&queue_mutex);
}

void
thread_pool_cleanup()
{
  keep_running = 0;

  pthread_mutex_lock(&queue_mutex);
  pthread_cond_broadcast(&queue_cond);
  pthread_mutex_unlock(&queue_mutex);

  if(threads != NULL) {
    for(int i = 0; i < current_pool_size; i++) {
      pthread_join(threads[i], NULL);
    }
    free(threads);
    threads = NULL;
  }

  if(task_queue != NULL) {
    free(task_queue);
    task_queue = NULL;
  }

  pthread_mutex_destroy(&queue_mutex);
  pthread_cond_destroy(&queue_cond);

  syslog(LOG_INFO, "Thread-pool cleared and resources released.");
}

