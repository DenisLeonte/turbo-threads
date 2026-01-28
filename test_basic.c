#include "uthread.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

void thread_func(void *arg) {
    int id = *(int *)arg;
    printf("Thread %d started\n", id);
    
    for (int i = 0; i < 5; i++) {
        printf("Thread %d: iteration %d\n", id, i);
        // Simulate some work (long enough to trigger preemption)
        for (volatile int j = 0; j < 30000000; j++);
    }
    
    printf("Thread %d finished\n", id);
}

int main() {
    printf("=== Basic Thread Test ===\n");
    
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
    
    printf("Main thread: All threads created, waiting...\n");
    
    // Wait a bit for threads to run (they should be scheduled automatically)
    for (volatile int i = 0; i < 500000000; i++);
    
    printf("Main thread: Test completed\n");
    return 0;
}
