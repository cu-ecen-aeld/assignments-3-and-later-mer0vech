#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
    
    struct thread_data *thread_args = (struct thread_data *) thread_param;
    int rv;

    usleep(thread_args->wait_to_obtain_ms * 1000);

    rv = pthread_mutex_lock(thread_args->mutex);
    if(rv !=0) {
        ERROR_LOG("Unable to lock (%d)", rv);
        thread_args->thread_complete_success = false;
        thread_args->exit_code = rv;
        return thread_args;
    }

    usleep(thread_args->wait_to_release_ms * 1000);

    rv = pthread_mutex_unlock(thread_args->mutex);
    if(rv != 0) {
        ERROR_LOG("Unable to unlock (%d)", rv);
        thread_args->thread_complete_success = false;
        thread_args->exit_code = rv;
        return thread_args;
    }

    thread_args->thread_complete_success = true;
    thread_args->exit_code = 0;

    return (void*)thread_args;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    int rv;
    struct thread_data *thread_params = malloc(sizeof(struct thread_data));

    if(thread_params == NULL) {
        ERROR_LOG("Unable to allocate thread data.");
        return false;
    }

    thread_params->thread_id = *thread;
    thread_params->mutex = mutex;
    thread_params->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_params->wait_to_release_ms = wait_to_release_ms;
    thread_params->thread_complete_success = false;
    thread_params->exit_code = 0;

    rv = pthread_create(thread, NULL, threadfunc, thread_params);

    if(rv != 0) {
        ERROR_LOG("Unable to create thread (%d)", rv);
        free(thread_params);
        return false;
    }


    return true;

}

