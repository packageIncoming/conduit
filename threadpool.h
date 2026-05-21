#pragma once

#include <pthread.h>
#include "epoll_handler.h"
#ifndef THREADPOOL 
#define THREADPOOL
#define NUM_THREADS 4
#define CONNECTIONS_PER_THREAD 20
#define MAX_CONNECTIONS  (NUM_THREADS * CONNECTIONS_PER_THREAD)


typedef struct task_s {
    connection_t* connection;
    struct task_s* next;
    struct task_s* prev;
} task_t;

typedef struct{
    int task_count;
    task_t* head; // dummy head
    task_t* tail; // Append new tasks to the end
} task_list_t; // FIFO Queue LinkedList implementation


typedef struct threadpool{
    int num_threads;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t* threads;
    int shutdown;
    int active_connections;
    task_list_t* task_list ;

} threadpool_t;

// allocates task_list_t
task_list_t* task_list_init(); 

// allocates pool, initializes mutex/condvar, spawns N worker threads
threadpool_t* threadpool_init(int num_threads);
// adds a task to the queue, signals one waiting worker
// NOTE: This method DOES NOT LOCK the mutex when enqueueing 
void threadpool_enqueue(threadpool_t* threadpool,task_t* task);

// pops task from head of pool; DOES NOT LOCK MUTEX WHEN DEQUEUEING
task_t* threadpool_dequeue(threadpool_t* pool);


// sets shutdown flag, broadcasts condvar, joins all threads, frees resources
void threadpool_destroy(threadpool_t* threadpool); 

// The routine that all threads run: wait for task->lock and perform OR close-up gracefully->unlock. 
void* thread_routine(void* arg);

#endif 