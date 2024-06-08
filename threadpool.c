#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>


threadpool* create_threadpool(int num_threads_in_pool) {
    if (num_threads_in_pool > MAXT_IN_POOL || num_threads_in_pool < 1){
        fprintf(stderr,"Usage: pool <pool-size> <number-of-tasks> <max-number-of-request>\n\n");
        return NULL;
    }
    threadpool* pool=(threadpool*)malloc(sizeof(threadpool));
    if (pool == NULL) {
        perror("error: <sys_call>\n");
        exit(EXIT_FAILURE);
    }
    pool->qsize=0;//initialize the list
	pool->shutdown=0;
    pool->dont_accept=0;
	pool->qhead=NULL;
	pool->qtail=NULL;
    pool->num_threads = num_threads_in_pool;
    pool->threads=(pthread_t*)malloc(pool->num_threads*sizeof(pthread_t));
    if(pool->threads==NULL) {
        perror("error: <sys_call>\n");
        free(pool);
        exit(EXIT_FAILURE);
    }
    if(pthread_cond_init(&(pool->q_empty),NULL)){// initialize mutex and condition variables
        perror("error: <sys_call>\n");
   		pthread_mutex_destroy(&(pool->qlock));
   		free(pool->threads);
   		free(pool);
   		return NULL;
	}
	if(pthread_cond_init(&(pool->q_not_empty),NULL)){
        perror("error: <sys_call>\n");
		pthread_cond_destroy(&(pool->q_empty));
		free(pool->threads);
   		free(pool);
   		return NULL;
   	}
	if(pthread_mutex_init(&(pool->qlock), NULL)){
        perror("error: <sys_call>\n");
		pthread_cond_destroy(&(pool->q_not_empty));
		pthread_cond_destroy(&(pool->q_empty));
   		free(pool->threads);
   		free(pool);
   		return NULL;
	}
    for(int i=0;i < num_threads_in_pool;i++) {//initialize the pool of threads 
        if (pthread_create(&(pool->threads[i]),NULL,do_work,pool)){
            perror("error: <sys_call>\n");
			pthread_cond_destroy(&(pool->q_empty));
			pthread_cond_destroy(&(pool->q_not_empty));
            pthread_mutex_destroy(&(pool->qlock));
			free(pool->threads);
   			free(pool);
			return NULL;
		}
    }
    return pool;
}
void dispatch(threadpool* from_me, dispatch_fn dispatch_to_here, void *arg){
    pthread_mutex_lock(&(from_me->qlock));  // critical section
    if(from_me->dont_accept == 1){
        pthread_mutex_unlock(&(from_me->qlock));  // release the lock before returning
        return;
    }
    pthread_mutex_unlock(&(from_me->qlock));

    if(!dispatch_to_here){
        printf("Dispatch not assigned correctly\n");
        destroy_threadpool(from_me);
    }
    work_t *work = (work_t*)malloc(sizeof(work_t));  // initialize new work
    if(!work) {
        perror("error: <sys_call>\n");
        return;
    }
    work->routine = dispatch_to_here;
    work->arg = arg;
    work->next = NULL;

    pthread_mutex_lock(&(from_me->qlock));  // acquire the lock again before modifying the queue
    if(from_me->dont_accept == 1){  
        free(work);
        pthread_mutex_unlock(&(from_me->qlock));  // release the lock before returning
        return;
    }
    if(!from_me->qsize){//add the job to the queue
        from_me->qhead = work;
        from_me->qtail = work;//if the list is empty
    }
    else{//if not add to the end of the queue
        from_me->qtail->next = work;
        from_me->qtail = from_me->qtail->next;
    }
    from_me->qsize++; //increase the size of the queue
    pthread_cond_signal(&(from_me->q_not_empty));
    pthread_mutex_unlock(&(from_me->qlock));
}

void* do_work(void* p){
    threadpool* pool = (threadpool*)p;
    while(1){
        pthread_mutex_lock(&(pool->qlock));  // Enter critical section
        while(pool->qsize == 0 && !pool->shutdown){  // Wait if queue is empty and not shutting down
            pthread_cond_wait(&(pool->q_not_empty), &(pool->qlock));
        }
        if(pool->shutdown){  // Check for shutdown flag
            pthread_mutex_unlock(&(pool->qlock));  // Exit critical section before returning
            pthread_exit(NULL);
            return NULL;
        }
        while(pool->qsize > 0){
            pool->qsize--;  // Decrement queue size
            work_t *temp = pool->qhead;  // Get the first job from the queue
            if(pool->qsize == 0){  // If queue becomes empty, update head and tail
                pool->qhead = NULL;
                pool->qtail = NULL;
                if(pool->dont_accept){  // Signal the destructor if necessary
                    pthread_cond_signal(&(pool->q_empty));
                }
            }
            else{
                pool->qhead = pool->qhead->next;  // Move head to the next job in the queue
            }
            pthread_mutex_unlock(&(pool->qlock));  // Exit critical section before executing the job
            temp->routine(temp->arg);
            free(temp);
            pthread_mutex_lock(&(pool->qlock));  // Re-enter critical section to check for more jobs
        }
        pthread_mutex_unlock(&(pool->qlock));  // Exit critical section
    }
    return NULL; 
}




void destroy_threadpool(threadpool* destroyme){
    pthread_mutex_lock(&(destroyme->qlock));//critical section
    destroyme->dont_accept=1;
    while(destroyme->qsize){
		pthread_cond_wait(&(destroyme->q_empty),&(destroyme->qlock));
    }
    destroyme->shutdown=1;
    pthread_cond_broadcast(&(destroyme->q_not_empty)); //tell them to continue
    pthread_mutex_unlock(&(destroyme->qlock));
	void *status;
	for(int i = 0;i <destroyme->num_threads;i++){
		pthread_join(destroyme->threads[i], &status);
    }
    pthread_mutex_destroy(&(destroyme->qlock));
	pthread_cond_destroy(&(destroyme->q_empty));
	pthread_cond_destroy(&(destroyme->q_not_empty));
	free(destroyme->threads);
	free(destroyme);
}