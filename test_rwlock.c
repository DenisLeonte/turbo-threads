#include "uthread.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

static int shared_data = 0;
static rwlock_t rwlock;

void reader_thread(void *arg) {
    int id = *(int *)arg;
    printf("Reader thread %d started\n", id);
    
    for (int i = 0; i < 5; i++) {
        uthread_rwlock_rdlock(&rwlock);
        
        // Read shared data
        int value = shared_data;
        printf("Reader %d: read value = %d\n", id, value);

        // Simulate reading work (long enough to trigger preemption)
        for (volatile int j = 0; j < 20000000; j++);

        uthread_rwlock_unlock(&rwlock);

        // Yield
        for (volatile int j = 0; j < 10000000; j++);
    }
    
    printf("Reader thread %d finished\n", id);
}

void writer_thread(void *arg) {
    int id = *(int *)arg;
    printf("Writer thread %d started\n", id);
    
    for (int i = 0; i < 3; i++) {
        uthread_rwlock_wrlock(&rwlock);
        
        // Write shared data
        shared_data++;
        printf("Writer %d: wrote value = %d\n", id, shared_data);

        // Simulate writing work (long enough to trigger preemption)
        for (volatile int j = 0; j < 20000000; j++);

        uthread_rwlock_unlock(&rwlock);

        // Yield
        for (volatile int j = 0; j < 10000000; j++);
    }
    
    printf("Writer thread %d finished\n", id);
}

int main() {
    printf("=== Read-Write Lock Test ===\n");
    
    uthread_rwlock_init(&rwlock);
    
    int reader_ids[2] = {1, 2};
    int writer_ids[2] = {1, 2};
    
    // Create 2 reader threads
    for (int i = 0; i < 2; i++) {
        int tid = uthread_create(reader_thread, &reader_ids[i]);
        if (tid < 0) {
            printf("Failed to create reader thread %d\n", i);
            return 1;
        }
        printf("Created reader thread with TID: %d\n", tid);
    }
    
    // Create 2 writer threads
    for (int i = 0; i < 2; i++) {
        int tid = uthread_create(writer_thread, &writer_ids[i]);
        if (tid < 0) {
            printf("Failed to create writer thread %d\n", i);
            return 1;
        }
        printf("Created writer thread with TID: %d\n", tid);
    }
    
    printf("Main thread: All threads created\n");
    
    // Wait for threads to complete
    for (volatile int i = 0; i < 1000000000; i++);
    
    printf("Final shared_data value: %d (expected: 6)\n", shared_data);
    
    if (shared_data == 6) {
        printf("Read-write lock test PASSED\n");
    } else {
        printf("Read-write lock test FAILED\n");
    }
    
    uthread_rwlock_destroy(&rwlock);
    return 0;
}
