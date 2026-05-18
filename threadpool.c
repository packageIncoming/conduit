#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "threadpool.h"


task_list_t* task_list_init(){
    task_list_t* taskpool = malloc(sizeof(task_list_t));
    taskpool->head = calloc(1,sizeof(task_t)); // dummy head
    taskpool->tail = NULL;
    taskpool->task_count=0;
    return taskpool;
} 

void* thread_routine(void* arg){
    threadpool_t* threadpool = (threadpool_t*)arg;
    while (1){
        pthread_mutex_lock(&threadpool->mutex);
        while (threadpool->task_list->task_count == 0 && !threadpool->shutdown) {
            pthread_cond_wait(&threadpool->cond, &threadpool->mutex);
        }
        if (threadpool->shutdown == 1 && threadpool->task_list->task_count==0){
            // Exit if shutdown and we have no more tasks to do
            pthread_mutex_unlock(&threadpool->mutex);
            break;
        }
        // Dequeue task if not shutting down 
        task_t* task = threadpool_dequeue(threadpool);
        pthread_mutex_unlock(&threadpool->mutex);
        
        connection_on_epollout(task->connection->fd,task->connection);
        close(task->connection->fd);
        connection_free(task->connection);
        free(task);
    }
    return (void*) 0;
}

threadpool_t* threadpool_init(int num_threads){
    // mallocs 
    threadpool_t* threadpool = calloc(1,sizeof(threadpool_t));
    threadpool->threads = calloc(num_threads, sizeof(pthread_t));
    threadpool->shutdown=0;
    threadpool->active_connections =0;
    threadpool->num_threads = num_threads;

    // inits
    pthread_mutex_init(&threadpool->mutex,NULL);
    pthread_cond_init(&threadpool->cond,NULL); 
    threadpool->task_list = task_list_init();

    // spawn the threads here
    for (int i =0; i < num_threads; i++){
        pthread_create(&threadpool->threads[i],NULL,thread_routine,(void *)threadpool);
    }



    return threadpool;
}


void threadpool_enqueue(threadpool_t* threadpool,task_t* task){
    task_list_t* taskpool = threadpool->task_list;
    if (taskpool->task_count == 0){
        // Adding to empty
        taskpool->head->next=task;
        task->prev=taskpool->head;
        taskpool->tail = task;
        task->next=NULL;
    }  else {
        // Add to tail
        taskpool->tail->next=task;
        task->prev = taskpool->tail;
        task->next=NULL;
        taskpool->tail = task;
    }
    
    taskpool->task_count+=1;
    pthread_cond_signal(&threadpool->cond);
}

task_t* threadpool_dequeue(threadpool_t* threadpool){
    task_list_t* task_list = threadpool->task_list;
    if (task_list->task_count == 0){
        fprintf(stderr,"attempt to dequeue from empty task list\n");
        return NULL;
    }
    task_t* real_head = task_list->head->next;
    // skip over it 
    task_list->head->next = real_head->next;
    if (real_head->next != NULL){
        real_head->next->prev = task_list->head;
    } else{
        task_list->tail= NULL;
    }
    real_head->next=NULL;
    real_head->prev=NULL;

    task_list->task_count-=1;
    return real_head;
}

void threadpool_destroy(threadpool_t* threadpool){  
    pthread_mutex_lock(&threadpool->mutex);
    threadpool->shutdown=1; // We are now shutting down
    pthread_cond_broadcast(&threadpool->cond);
    pthread_mutex_unlock(&threadpool->mutex);
    // Tell all threads we have shut down

    //Reclaim threads
    for(int i =0;i <threadpool->num_threads;i++){
        pthread_join(threadpool->threads[i],NULL);
    }
    free(threadpool->threads);

    // Free task list 
    free(threadpool->task_list->head);
    if (threadpool->task_list->task_count > 0){
        task_t* curr = threadpool->task_list->head->next;
        while (curr!= NULL){
            task_t* nxt = curr->next;
            connection_free(curr->connection);
            free(curr);
            curr=nxt;
        }
    }


    // Free threadpool itself
    pthread_mutex_destroy(&threadpool->mutex);
    pthread_cond_destroy(&threadpool->cond);
    free(threadpool);

}
