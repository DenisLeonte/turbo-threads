#ifndef UTHREAD_H
#define UTHREAD_H

#include <ucontext.h>
#include <signal.h>
#include <stdbool.h>

// Thread states
typedef enum {
    THREAD_READY,
    THREAD_RUNNING,
    THREAD_BLOCKED,
    THREAD_TERMINATED
} thread_state_t;

// Thread structure
typedef struct thread {
    int tid;                    // Thread ID
    ucontext_t context;         // Thread context
    thread_state_t state;       // Thread state
    void *stack;                // Stack pointer
    size_t stack_size;          // Stack size
    void *retval;               // Return value
    void (*start_routine)(void *); // Thread start function
    void *arg;                  // Thread argument
    struct thread *next;        // Next thread in queue
    struct thread *waiting_for; // Thread waiting for this thread (for join)
    struct mutex *blocked_on;   // Mutex this thread is blocked on
    struct rwlock *blocked_on_rw; // RW lock this thread is blocked on
    bool is_writer;            // For RW locks: true if waiting for write lock
} thread_t;

// Mutex structure
typedef struct mutex {
    int locked;                 // 0 = unlocked, 1 = locked
    thread_t *owner;             // Thread that owns the mutex
    thread_t *waiting_list;     // List of threads waiting for this mutex
} mutex_t;

// Read-write lock structure
typedef struct rwlock {
    int readers;                // Number of active readers
    thread_t *writer;           // Current writer thread (NULL if none)
    thread_t *read_waiting;     // Readers waiting
    thread_t *write_waiting;    // Writers waiting
    thread_t *readers_list;     // List of threads holding read lock
} rwlock_t;

// Thread functions
int uthread_create(void (*start_routine)(void *), void *arg);
int uthread_join(int tid, void **retval);
void uthread_exit(void *retval);
int uthread_self(void);

// Mutex functions
int uthread_mutex_init(mutex_t *mutex);
int uthread_mutex_lock(mutex_t *mutex);
int uthread_mutex_unlock(mutex_t *mutex);

// Read-write lock functions
int uthread_rwlock_init(rwlock_t *rwlock);
int uthread_rwlock_rdlock(rwlock_t *rwlock);
int uthread_rwlock_wrlock(rwlock_t *rwlock);
int uthread_rwlock_unlock(rwlock_t *rwlock);
int uthread_rwlock_destroy(rwlock_t *rwlock);

// Internal scheduler functions
void scheduler_init(void);
void scheduler_yield(void);
void scheduler_schedule(void);
void deadlock_detect(void);

#endif // UTHREAD_H
