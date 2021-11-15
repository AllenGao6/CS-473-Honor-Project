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
#define LOCK_INIT {ATOMIC_FLAG_INIT, NULL, NULL}

///////// Type Defination //////////
struct Thread {
    struct Thread *next;
    ucontext_t context;
    int thread_id;
};

struct __lock_t {
    atomic_flag flag;
    
    struct Thread *block_head;
    struct Thread *block_tail;
};
// parameter for virtual thread to set up their local variable
typedef struct {
   int id;

} threadparm_t;

typedef struct __lock_t lock_t;

static bool is_terminated = false;

//////// Global Variable ///////////
int thread_counter = 0;
struct Thread *ready_head = NULL;
struct Thread *ready_tail = NULL;
struct Thread *running = NULL;

// Thread local variable
__thread int thread_id;
__thread struct Thread *thread_running;
pthread_mutex_t mutex_queue = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_non_empty = PTHREAD_COND_INITIALIZER;

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
    

    printf("Create/start threads\n");
    for (int i=0; i < NUM_VIRTUAL_THREADS; i++) { 
        /* Create per-thread TLS data and pass it to the thread */
        gData[i].id = i+1;
        int rc = pthread_create(&thread[i], NULL, virtual_thread, &gData[i]);
        printf("Thread %d created\n", i);
        checkResults("pthread_create()\n", rc);
    }
 
}

void task_thread_init() {

    // Start one other thread
    thread_create(&test_thread); // thread 1
    // sleep for 1 second
    thread_create(&test_thread); // thread 2
    thread_create(&test_thread); // thread 3
    
    printf("Main returned from thread_create\n");

    printf("Main calling thread_yield\n");

    thread_yield();
    thread_yield();
    thread_yield();
}

void *virtual_thread(void *parm){
    // recieve the thread parameter
    threadparm_t *gData;
    gData = (threadparm_t *)parm;
    thread_id = gData->id;
    thread_running = NULL;

    while(!is_terminated) {

        pthread_mutex_lock(&mutex_queue);

        while(ready_head == NULL) {
            // add conditioning variable here to check if the ready queue is empty
            pthread_cond_wait(&queue_non_empty, &mutex_queue);
        }

        // dequeue the first thread in the ready queue
        thread_running = dequeue(&ready_head, &ready_tail);
        printf("Virtual thread %d is running\n", thread_running->thread_id);
        // set the context of the running thread
        setcontext(&thread_running->context);

        printf("Virtual Thread %d is running\n", thread_id);
        printf("test_var: %d\n", test_var);
        test_var++;
        thread_yield();

        // unlock the mutex
        pthread_mutex_unlock(&mutex_queue);
        
    }
    printf("Virtual Thread %d is exiting\n", thread_id);
}

void lock(lock_t *lock)
{

    // loop until block clear
    while (atomic_flag_test_and_set(&lock->flag))
    {
        // put the running block to block queue if blocked
        enqueue(&lock->block_head, &lock->block_tail, running);

        // the ready queue shouldn't be empty
        if (ready_head != NULL && ready_tail != NULL)
        {
            // then yield to another thread at ready queue
            struct Thread* old_thread = running;

            running = dequeue(&ready_head, &ready_tail);

            printf("thread %d blocked: yield to head of ready queue - thread %d\n", 
                old_thread->thread_id,running->thread_id);
            swapcontext(&old_thread->context, &running->context);
        } else {
            printf("ready queue should not be empty while the critical section is locked!\n");
        }
    }
}

void unlock(lock_t *lock)
{
    atomic_flag_clear(&lock->flag);

    // yield to the head of blocking queue
    if (lock->block_head != NULL)
    {
        struct Thread* old_thread = running;
        enqueue(&ready_head, &ready_tail, old_thread);

        running = dequeue(&lock->block_head, &lock->block_tail);

        printf("thread %d unlock: yield to head of block queue %d\n", 
            old_thread->thread_id,running->thread_id);
        swapcontext(&old_thread->context, &running->context);
    } else
    {
        printf("thread %d unlock and continue: empty block queue\n", running->thread_id);
    }
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

  
    // initialize the thread parameter for each thread
    pthread_t thread[NUM_VIRTUAL_THREADS];
    threadparm_t gData[NUM_VIRTUAL_THREADS];

    // initialize pthread as virtual thread to handle the ready_queue
    thread_init(thread, gData);

    // ---------- ---------- ---------- ---------- ---------- ---------- ---------- ---------- ----------

    printf("Main calling task thread_create\n");

    // create thread for task
    task_thread_init();

    // ---------- ---------- ---------- ---------- ---------- ---------- ---------- ---------- ----------
    printf("Wait for the threads to complete, and release their resources\n");
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
    printf("Thread %d entering the critical section.\n", running->thread_id);
    //critical section
    for (int i = 0; i < 5000; i++)
        test_var += 1;

    printf("Test_thread calling thread_yield\n");
    thread_yield();
    printf("Thread %d return from yield.\n", running->thread_id);

    for (int i = 0; i < 5000; i++)
        test_var += 1;

    //unlocking
    unlock(&lock1);

    printf("Exiting critical section. test_val: %d\n", test_var);

    thread_exit(0);
}

// Yield to another thread
void thread_yield() {
    if (ready_head == NULL && ready_tail == NULL) {
        printf("Ready Queue is empty, return from yield()\n");
        return;
    }
    struct Thread* old_thread = running;
    enqueue(&ready_head, &ready_tail, old_thread);
    
    running = dequeue(&ready_head, &ready_tail);

    printf("Thread %d yielding to thread %d\n", old_thread->thread_id, running->thread_id);
    
    // This will stop us from running and restart the other thread
    swapcontext(&old_thread->context, &running->context);

    // The other thread yielded back to us
    printf("Thread %d back in thread_yield\n", running->thread_id);
}

// Create a thread
int thread_create(void (*thread_function)(void)) {
    if ((thread_counter + 1) >= MAX_THREAD) return -1;

    thread_counter++;

    struct Thread *new_thread = malloc(sizeof(struct Thread));
    new_thread->thread_id = thread_counter;
    enqueue(&ready_head, &ready_tail, new_thread);

    printf("Thread %d in thread_create, new thread: %d\n", running->thread_id, new_thread->thread_id);
    
    printf("Thread %d calling getcontext and makecontext\n", running->thread_id);

    // First, create a valid execution context the same as the current one
    getcontext(&(new_thread->context));

    // maybe we should set up uclink?

    // Now set up its stack
    new_thread->context.uc_stack.ss_sp = malloc(8192);
    new_thread->context.uc_stack.ss_size = 8192;

    // Now create the new context and specify what function it should run
    makecontext(&(new_thread->context), test_thread, 0);
    
    printf("Thread %d done with thread_create\n", running->thread_id);
}

// exit the current thread and delete its context 
void thread_exit(int status) {
    printf("Thread %d in thread_exit\n", running->thread_id);
    // delete the context
    free(running->context.uc_stack.ss_sp);
    free(running);

    // set the running thread to NULL
    running = NULL;

    // yield to another thread
    thread_yield();
}

void enqueue(struct Thread **head, struct Thread **tail, struct Thread* newT)
{
    newT->next = NULL;

    if (*head == NULL && *tail == NULL)
    {
        *head = newT;
        *tail = newT;
    } else 
    {
        (*tail)->next = newT;
        *tail = (*tail)->next;
    }
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
