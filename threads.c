//  threads.c
//  Created by Scott Brandt on 5/6/13.

#include <stdio.h>
#include <stdlib.h>

#define _XOPEN_SOURCE
#include <ucontext.h>
#define MAX_THREAD 4

static ucontext_t ctx[MAX_THREAD];
typedef uint ctxIdx;
static ctxIdx active_thread = 0;
static ctxIdx thread_count = 0;

static void test_thread(void);
void thread_exit(int);
int thread_yield(ctxIdx nextCtx);
int thread_create(void (*thread_function)(void));



// This is the main thread
// In a real program, it should probably start all of the threads and then wait for them to finish
// without doing any "real" work
int main(void) {
    printf("Main starting\n");
    
    printf("Main calling thread_create\n");

    // Start one other thread
    thread_create(&test_thread);
    thread_create(&test_thread);
    thread_create(&test_thread);
    
    printf("Main returned from thread_create\n");

    printf("Main calling thread_yield\n");
    
    thread_yield(1); printf("\n");
    thread_yield(2); printf("\n");
    thread_yield(3); printf("\n");
    
    printf("Main returned from thread_yield\n");

    // We should never get here
    exit(0);
}

// This is the thread that gets started by thread_create
static void test_thread(void) {
    printf("In test_thread\n");
        
    printf("Test_thread calling thread_yield\n");
    
    thread_yield(0);
    
    printf("Test_thread returned from thread_yield\n");
    
    thread_exit(0);
}

// Yield to another thread
int thread_yield(ctxIdx nextCtx) {
    int old_thread = active_thread;
    
    // This is the scheduler, it is a bit primitive right now
    active_thread = nextCtx;

    printf("Thread %d yielding to thread %d\n", old_thread, active_thread);
    printf("Thread %d calling swapcontext\n", old_thread);
    
    // This will stop us from running and restart the other thread
    swapcontext(&ctx[old_thread], &ctx[active_thread]);

    // The other thread yielded back to us
    printf("Thread %d back in thread_yield\n", active_thread);
}

// Create a thread
int thread_create(void (*thread_function)(void)) {
    if (thread_count + 1 >= MAX_THREAD) return -1;

    thread_count++;
    int newthread = thread_count;
    
    printf("Thread %d in thread_create, new thread: %d\n", active_thread, newthread);
    
    printf("Thread %d calling getcontext and makecontext\n", active_thread);

    // First, create a valid execution context the same as the current one
    getcontext(&ctx[newthread]);

    // Now set up its stack
    ctx[newthread].uc_stack.ss_sp = malloc(8192);
    ctx[newthread].uc_stack.ss_size = 8192;

    // This is the context that will run when this thread exits
    ctx[newthread].uc_link = &ctx[active_thread];

    // Now create the new context and specify what function it should run
    makecontext(&ctx[newthread], test_thread, 0);
    
    printf("Thread %d done with thread_create\n", active_thread);
}

// This doesn't do anything at present
void thread_exit(int status) {
    printf("Thread %d exiting\n", active_thread);
}
