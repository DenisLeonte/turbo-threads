#include "uthread.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

static mutex_t mutex1, mutex2;

void thread1_func() {
    printf("Thread 1: Acquiring mutex1...\n");
    uthread_mutex_lock(&mutex1);
    printf("Thread 1: Got mutex1\n");
    
    // Simulate some work
    for (volatile int i = 0; i < 50000000; i++);
    
    printf("Thread 1: Trying to acquire mutex2...\n");
    uthread_mutex_lock(&mutex2);
    printf("Thread 1: Got mutex2\n");
    
    uthread_mutex_unlock(&mutex2);
    uthread_mutex_unlock(&mutex1);
    
    printf("Thread 1: Finished\n");
}

void thread2_func() {
    printf("Thread 2: Acquiring mutex2...\n");
    uthread_mutex_lock(&mutex2);
    printf("Thread 2: Got mutex2\n");
    
    // Simulate some work
    for (volatile int i = 0; i < 50000000; i++);
    
    printf("Thread 2: Trying to acquire mutex1...\n");
    uthread_mutex_lock(&mutex1);
    printf("Thread 2: Got mutex1\n");
    
    uthread_mutex_unlock(&mutex1);
    uthread_mutex_unlock(&mutex2);
    
    printf("Thread 2: Finished\n");
}

int main() {
    printf("=== Deadlock Detection Test ===\n");
    printf("This test creates a potential deadlock scenario.\n");
    printf("Send SIGQUIT (Ctrl+\\) to trigger deadlock detection report.\n\n");
    
    uthread_mutex_init(&mutex1);
    uthread_mutex_init(&mutex2);
    
    // Create two threads that will deadlock
    int tid1 = uthread_create(thread1_func, NULL);
    int tid2 = uthread_create(thread2_func, NULL);
    
    if (tid1 < 0 || tid2 < 0) {
        printf("Failed to create threads\n");
        return 1;
    }
    
    printf("Created threads: %d and %d\n", tid1, tid2);
    printf("Main thread: Waiting... (send SIGQUIT to check for deadlock)\n");
    
    // Wait indefinitely (user can send SIGQUIT)
    while (1) {
        for (volatile int i = 0; i < 100000000; i++);
        printf("Main thread: still running... (send SIGQUIT with Ctrl+\\)\n");
    }
    
    return 0;
}
