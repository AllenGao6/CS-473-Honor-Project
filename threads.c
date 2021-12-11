//  threads.c
// Allen Gao, Weisheng Li's Honor Project

#define _MULTI_THREADED

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>

#define _XOPEN_SOURCE
#include <ucontext.h>
#include <stdatomic.h>
#include <pthread.h>


#define MAX_THREAD 50
#define NUM_VIRTUAL_THREADS 3
#define LOCK_INIT {ATOMIC_FLAG_INIT, QUEUE_INIT}
#define QUEUE_INIT {NULL, NULL, PTHREAD_MUTEX_INITIALIZER}
#define THREAD_EXIT (void*)1

///////// Type Defination //////////
struct Thread {
    struct Thread *next;
    ucontext_t context;
    int thread_id;
};

typedef struct {
    struct Thread *head;
    struct Thread *tail;
    pthread_mutex_t queue_lock;
} queue_t;

typedef struct {
    atomic_flag flag;
    queue_t block_queue;
} lock_t;

// parameter for virtual thread to set up their local variable
typedef struct {
   int id;

} threadparm_t;


static bool is_terminated = false;

//////// Global Variable ///////////
static int thread_counter = 0;
static queue_t ready_queue = QUEUE_INIT;
static pthread_cond_t queue_non_empty = PTHREAD_COND_INITIALIZER;

// struct Thread *running = NULL;

// Thread local variable
__thread int thread_id;
__thread struct Thread *thread_running;
__thread ucontext_t main_thread_context;
__thread int was_switched = 0;
__thread queue_t* ret_queue; // specify where to put the return task

//test variable
static int test_var = 0;

////// Function Declaration ////////
static void test_thread(void);
void thread_exit(int);
void thread_yield();
int thread_create(void (*thread_function)(void));
void thread_init();
void lock(lock_t *lock);
void unlock(lock_t *lock);
void *virtual_thread(void *arg);
void task_thread_init();
// queue functions declaration
void enqueue(struct Thread **head, struct Thread **tail, struct Thread* newT);
struct Thread* dequeue(struct Thread **head, struct Thread **tail);


///////// Function Definition ////////

static void checkResults(char* string, int val) {             
 if (val) {                                     
   printf("Failed with %d at %s", val, string);                                    
 }                                              
}

void thread_init(pthread_t *thread, threadparm_t *gData) {
    

    printf("Create/start Virtual CPUs\n");
    for (int i=0; i < NUM_VIRTUAL_THREADS; i++) { 
        /* Create per-thread TLS data and pass it to the thread */
        gData[i].id = i+1;
        int rc = pthread_create(&thread[i], NULL, virtual_thread, &gData[i]);
        printf("Virtual CPU %d created\n", i+1);
        checkResults("pthread_create()\n", rc);
    }
}

void task_thread_init(int num_task_threads) {

    for(int i = 0; i < num_task_threads; i++) {
        thread_create(test_thread);
    }
    
    printf("Main returned from thread_create\n");
}

void *virtual_thread(void *parm){
    // recieve the thread parameter
    threadparm_t *gData;
    gData = (threadparm_t *)parm;
    thread_id = gData->id;
    thread_running = NULL;
    getcontext(&main_thread_context);

    printf("Virtual CPU %d initialized\n", thread_id);
    while(true) {

        pthread_mutex_lock(&ready_queue.queue_lock);
       
        if(is_terminated) {
            pthread_mutex_unlock(&ready_queue.queue_lock);
            break;
        }
        while(ready_queue.head == NULL) {
            printf("Virtual CPU %d is waiting\n", thread_id);
            // add conditioning variable here to check if the ready queue is empty
            pthread_cond_wait(&queue_non_empty, &ready_queue.queue_lock);
            if(is_terminated) {
                pthread_mutex_unlock(&ready_queue.queue_lock);
                goto exit;
            }
        }
        // dequeue the first thread in the ready queue
        // this part is wrong
        thread_running = dequeue(&ready_queue.head, &ready_queue.tail);
        pthread_mutex_unlock(&ready_queue.queue_lock);
        
        printf("Virtual CPU %d is about to execute Task thread %d\n", 
            thread_id, thread_running->thread_id);
        // start executing the task thread
        swapcontext(&main_thread_context, &thread_running->context);   

        // ret_queue could be either block queue or ready queue
        // it's set by previous running thread (before swapcontext return)
        
        // by default, ret_queue set to ready queue
        if (ret_queue != THREAD_EXIT) 
        {
            printf("Virtual CPU %d yield from Task thread %d\n", 
                thread_id, thread_running->thread_id);

            // by default, return the task to ready queue
            if (ret_queue == NULL)
                ret_queue = &ready_queue;

            pthread_mutex_lock(&ret_queue->queue_lock);
            enqueue(&ret_queue->head, &ret_queue->tail, thread_running);
            // reset thread_running
            thread_running = NULL;
            if (ret_queue == &ready_queue)
                pthread_cond_signal(&queue_non_empty);
            pthread_mutex_unlock(&ret_queue->queue_lock);

            // reset ret_queue
            ret_queue = NULL; 
        } else {
            printf("Virtual CPU %d finishes executing Task thread %d\n", 
                thread_id, thread_running->thread_id);
        }

        // if ret_queue == THREAD_EXIT,
        // do not put it back to any queue
    }
    exit:
    
    printf("Virtual CPu %d is exiting\n", thread_id);
}

void lock(lock_t *lock)
{
    // loop until block clear
    while (atomic_flag_test_and_set(&lock->flag))
    {
        ret_queue = &lock->block_queue;

        printf("thread %d is blocked, yield\n", thread_running->thread_id);
        thread_yield();
    }
}

void unlock(lock_t *lock)
{
    atomic_flag_clear(&lock->flag);

    // yield to the head of blocking queue
    if (lock->block_queue.head != NULL)
    {
        pthread_mutex_lock(&lock->block_queue.queue_lock);
        struct Thread* blockQueue_head = dequeue(&lock->block_queue.head, &lock->block_queue.tail);
        pthread_mutex_unlock(&lock->block_queue.queue_lock);

        pthread_mutex_lock(&ready_queue.queue_lock);
        enqueue(&ready_queue.head, &ready_queue.tail, blockQueue_head);
        pthread_cond_signal(&queue_non_empty);
        pthread_mutex_unlock(&ready_queue.queue_lock);

        printf("thread %d unlock and thread %d now in ready_queue\n", 
            thread_running->thread_id, blockQueue_head->thread_id);
    } else
        printf("thread %d unlock and continue: empty block queue\n", 
            thread_running->thread_id);
}

lock_t lock1 = LOCK_INIT;

// This is the main thread
// In a real program, it should probably start all of the threads and then wait for them to finish
// without doing any "real" work
int main(void) {
    printf("Main starting\n");
    
    // create thread for main itself
    // Do we want to use main as a thread itself? I am thinking we can use main as a control center to manage other threads
    // struct Thread *master_thread = malloc(sizeof(struct Thread));
    // getcontext(&(master_thread->context));
    // master_thread->next = NULL;
    // master_thread->thread_id = 0;

  
    printf("Main calling task thread_create\n");

    // create thread for task
    task_thread_init(5);
    // initialize the thread parameter for each thread
    pthread_t thread[NUM_VIRTUAL_THREADS];
    threadparm_t gData[NUM_VIRTUAL_THREADS];

    // initialize pthread as virtual thread to handle the ready_queue
    thread_init(thread, gData);

    for (int i=0; i < NUM_VIRTUAL_THREADS; i++) {
        int rc = pthread_join(thread[i], NULL);
        checkResults("pthread_join()\n", rc);
    }

    printf("Main completed\n");
    return 0;
}

// This is the thread that gets started by thread_create
static void test_thread(void) {
    printf("In test_thread\n");

    //adding lock
    lock(&lock1);
    printf("Thread %d entering the critical section.\n", thread_running->thread_id);
    //critical section
    for (int i = 0; i < 5000; i++)
        test_var += 1;
    sleep(2);

    printf("Exiting critical section. test_val: %d\n", test_var);
    unlock(&lock1);

    thread_exit(0);
}

// Yield back to main thread
void thread_yield() {
    struct Thread* old_thread = thread_running;

    // This will stop us from running and restart the other thread
    swapcontext(&old_thread->context,&main_thread_context);
}

// Create a thread
int thread_create(void (*thread_function)(void)) {
    printf("In thread_create\n");
    if ((thread_counter + 1) >= MAX_THREAD) return -1;

    thread_counter++;
    printf("1_");
    struct Thread *new_thread = malloc(sizeof(struct Thread));
    new_thread->thread_id = thread_counter;
    enqueue(&ready_queue.head, &ready_queue.tail, new_thread);
    printf("2_");

    // First, create a valid execution context the same as the current one
    getcontext(&(new_thread->context));
    printf("3_\n");

    // Now set up its stack
    new_thread->context.uc_stack.ss_sp = malloc(8192);
    new_thread->context.uc_stack.ss_size = 8192;

    // Now create the new context and specify what function it should run
    makecontext(&(new_thread->context), test_thread, 0);
}

// exit the current thread and delete its context 
void thread_exit(int status) {
    printf("Thread %d exits with status: %d\n", 
        thread_running->thread_id, status);

    // delete the context
    free(thread_running->context.uc_stack.ss_sp);
    free(thread_running);

    ret_queue = THREAD_EXIT;

    // yield to another thread
    thread_yield();
}

void enqueue(struct Thread **head, struct Thread **tail, struct Thread* newT)
{
    newT->next = NULL;
    // if(running == NULL){
    //     running = newT;
    // }else{
    if (*head == NULL && *tail == NULL)
    {
        *head = newT;
        *tail = newT;
    } else 
    {
        (*tail)->next = newT;
        *tail = (*tail)->next;
    }
    // }
}

struct Thread* dequeue(struct Thread **head, struct Thread **tail)
{
    if (head == NULL && tail == NULL)
        return NULL;
    else
    {
        struct Thread* removed_blk = *head;
        *head = (*head)->next;
        removed_blk->next = NULL;

        // edge case: remove the last element in the queue
        if (*head == NULL) *tail = NULL;

        return removed_blk;
    }
}
