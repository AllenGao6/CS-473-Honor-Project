//  threads.c
//  Created by Scott Brandt on 5/6/13.

#include <stdio.h>
#include <stdlib.h>

#define _XOPEN_SOURCE
#include <ucontext.h>

#define MAX_THREAD 50

struct Thread {
    struct Thread *next;
    ucontext_t context;
    int thread_id;
};

int thread_counter = 0;
struct Thread *ready_head = NULL;
struct Thread *ready_tail = NULL;

struct Thread *block_head = NULL;
struct Thread *block_tail = NULL;

struct Thread *running = NULL;

static void test_thread(void);
void thread_exit(int);
void thread_yield();
int thread_create(void (*thread_function)(void));


// This is the main thread
// In a real program, it should probably start all of the threads and then wait for them to finish
// without doing any "real" work
int main(void) {
    printf("Main starting\n");
    
    // create thread for main itself
    struct Thread *master_thread = malloc(sizeof(struct Thread));
    getcontext(&(master_thread->context));
    master_thread->next = NULL;
    master_thread->thread_id = 0;

    running = master_thread;
    
    printf("Main calling thread_create\n");

    // Start one other thread
    thread_create(&test_thread); // thread 1
    thread_create(&test_thread); // thread 2
    thread_create(&test_thread); // thread 3
    
    printf("Main returned from thread_create\n");

    printf("Main calling thread_yield\n");
    
    thread_yield();
    
    printf("Main returned from thread_yield\n");

    exit(0);
}

// This is the thread that gets started by thread_create
static void test_thread(void) {
    printf("In test_thread\n");
        
    printf("Test_thread calling thread_yield\n");
    
    thread_yield();
    
    printf("Test_thread returned from thread_yield\n");
    
    thread_exit(0);
}

//block the running thread due to some events
void thread_block(){

    //if block queue is empty
    if(block_head == NULL && block_tail == NULL){
        block_head = running;
        block_tail = running;
    }else{
        block_tail->next = running;
        block_tail = block_tail->next;
    }

    //move the first in ready queue to the running queue
    if(ready_head != NULL && ready_tail != NULL){
        running = ready_head;
        ready_head = ready_head->next;
        if(ready_head == NULL)
            ready_tail == NULL;
        running->next = NULL;
    }else{
        running = NULL;
        printf("No thread is ready queue");
    }
}

// Yield to another thread
void thread_yield() {
    struct Thread* old_thread = running;
    
    // This is the scheduler, it is a bit primitive right now
    if(ready_head != NULL && ready_tail != NULL){
        // add running thread to the end of ready queue
        ready_tail->next = running;
        ready_tail = ready_tail->next;

        // switch running thread to the head of ready queue
        running = ready_head;
        ready_head = ready_head->next;
    } else {
        printf("Ready Queue is empty, return from yield()\n");
        return;
    }

    printf("Thread %d yielding to thread %d\n", old_thread->thread_id, running->thread_id);
    printf("Thread %d calling swapcontext\n", old_thread->thread_id);
    
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

    // if there is not element in ready queue
    if(ready_head == NULL && ready_tail == NULL){
        ready_head = new_thread;
        ready_tail = new_thread;
    }else{
        ready_tail->next = new_thread;
        ready_tail = ready_tail->next;
    }

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
    printf("Thread %d exiting\n", running->thread_id);
    /*
    // free the thread block
    struct Thread* return_thread = running->context.uc_link;
    free(running);

    // return to the thread pointed by uclink
    setcontext(return_thread);
    */
}
