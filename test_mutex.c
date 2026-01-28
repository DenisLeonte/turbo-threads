#include "uthread.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

static int counter = 0;
static mutex_t mutex;

void thread_func(void *arg) {
    int id = *(int *)arg;
    printf("Thread %d started\n", id);
    
    for (int i = 0; i < 10; i++) {
        uthread_mutex_lock(&mutex);

        // Critical section
        int temp = counter;
        for (volatile int j = 0; j < 1000000; j++); // Simulate work
        counter = temp + 1;
        printf("Thread %d: counter = %d\n", id, counter);

        uthread_mutex_unlock(&mutex);

        // Yield to other threads (long enough to trigger preemption)
        for (volatile int j = 0; j < 30000000; j++);
    }
    
    printf("Thread %d finished\n", id);
}

int main() {
    printf("=== Mutex Test ===\n");
    
    uthread_mutex_init(&mutex);
    
    int ids[3];
    
    // Create 3 threads
    for (int i = 0; i < 3; i++) {
        ids[i] = i + 1;
        int tid = uthread_create(thread_func, &ids[i]);
        if (tid < 0) {
            printf("Failed to create thread %d\n", i);
            return 1;
        }
        printf("Created thread with TID: %d\n", tid);
    }
    
    printf("Main thread: All threads created\n");
    
    // Wait for threads to complete
    for (volatile int i = 0; i < 1000000000; i++);
    
    printf("Final counter value: %d (expected: 30)\n", counter);
    
    if (counter == 30) {
        printf("Mutex test PASSED\n");
    } else {
        printf("Mutex test FAILED\n");
    }
    
    return 0;
}
