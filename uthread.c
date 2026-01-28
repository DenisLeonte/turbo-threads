#define _XOPEN_SOURCE 700
#include "uthread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>

#define STACK_SIZE (8 * 1024)  // 8KB
#define MAX_THREADS 128
#define QUANTUM_US 10000       // 10ms

static thread_t *ready_queue = NULL;
static thread_t *running_thread = NULL;
static thread_t threads[MAX_THREADS];
static int next_tid = 1;
static int thread_count = 0;
static bool scheduler_initialized = false;
static ucontext_t main_context;
static struct itimerval timer;
static thread_t *thread_to_free = NULL; // Thread to be freed

static void thread_wrapper(void);
static void timer_handler(int sig);
static void sigquit_handler(int sig);
static thread_t *find_thread(int tid);
static void enqueue_thread(thread_t *thread);
static thread_t *dequeue_thread(void);
static void unblock_thread(thread_t *thread);
static void print_deadlock_report(void);

static void block_signals(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    sigprocmask(SIG_BLOCK, &set, NULL);
}

static void unblock_signals(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
}

void scheduler_init(void) {
    if (scheduler_initialized) return;
    
    running_thread = &threads[0];
    running_thread->tid = 0;
    running_thread->state = THREAD_RUNNING;
    running_thread->stack = NULL;
    running_thread->start_routine = NULL;
    running_thread->arg = NULL;
    running_thread->next = NULL;
    running_thread->waiting_for = NULL;
    running_thread->blocked_on = NULL;
    running_thread->blocked_on_rw = NULL;
    getcontext(&running_thread->context);
    getcontext(&main_context);
    thread_count = 1;
    
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = timer_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; 
    sigaction(SIGALRM, &sa, NULL);

    struct sigaction sa_quit;
    memset(&sa_quit, 0, sizeof(sa_quit));
    sa_quit.sa_handler = sigquit_handler;
    sigemptyset(&sa_quit.sa_mask);
    sigaddset(&sa_quit.sa_mask, SIGALRM); // Block timer during deadlock report
    sa_quit.sa_flags = SA_RESTART;
    sigaction(SIGQUIT, &sa_quit, NULL);
    
    // Setup timer for preemption
    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = QUANTUM_US;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = QUANTUM_US;
    setitimer(ITIMER_REAL, &timer, NULL);
    
    scheduler_initialized = true;
}

static void timer_handler(int sig) {
    (void)sig;
    scheduler_yield();
}

static void sigquit_handler(int sig) {
    (void)sig;
    print_deadlock_report();
}

static thread_t *find_thread(int tid) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == tid && threads[i].state != THREAD_TERMINATED) {
            return &threads[i];
        }
    }
    return NULL;
}

static void enqueue_thread(thread_t *thread) {
    if (ready_queue == NULL) {
        ready_queue = thread;
        thread->next = NULL;
    } else {
        thread_t *current = ready_queue;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = thread;
        thread->next = NULL;
    }
}

static thread_t *dequeue_thread(void) {
    if (ready_queue == NULL) {
        return NULL;
    }
    thread_t *thread = ready_queue;
    ready_queue = ready_queue->next;
    thread->next = NULL;
    return thread;
}

static void unblock_thread(thread_t *thread) {
    if (thread && thread->state == THREAD_BLOCKED) {
        thread->state = THREAD_READY;
        thread->blocked_on = NULL;
        thread->blocked_on_rw = NULL;
        enqueue_thread(thread);
    }
}

int uthread_create(void (*start_routine)(void *), void *arg) {
    block_signals();

    if (!scheduler_initialized) {
        scheduler_init();
    }
    
    if (thread_count >= MAX_THREADS) {
        unblock_signals();
        return -1;
    }
    
    // Find free slot
    thread_t *new_thread = NULL;
    for (int i = 1; i < MAX_THREADS; i++) {
        if (threads[i].tid == 0 || threads[i].state == THREAD_TERMINATED) {
            new_thread = &threads[i];
            break;
        }
    }
    
    if (new_thread == NULL) {
        unblock_signals();
        return -1;
    }
    
    // Allocate stack
    new_thread->stack = malloc(STACK_SIZE);
    if (new_thread->stack == NULL) {
        unblock_signals();
        return -1;
    }
    
    new_thread->tid = next_tid++;
    new_thread->state = THREAD_READY;
    new_thread->stack_size = STACK_SIZE;
    new_thread->retval = NULL;
    new_thread->start_routine = start_routine;
    new_thread->arg = arg;
    new_thread->next = NULL;
    new_thread->waiting_for = NULL;
    new_thread->blocked_on = NULL;
    new_thread->blocked_on_rw = NULL;
    new_thread->is_writer = false;
    
    getcontext(&new_thread->context);
    new_thread->context.uc_stack.ss_sp = new_thread->stack;
    new_thread->context.uc_stack.ss_size = STACK_SIZE;
    new_thread->context.uc_link = NULL; 
    
    sigemptyset(&new_thread->context.uc_sigmask);
    
    makecontext(&new_thread->context, (void (*)(void))thread_wrapper, 0);
    
    enqueue_thread(new_thread);
    thread_count++;
    
    unblock_signals();
    return new_thread->tid;
}

static void thread_wrapper(void) {
    if (running_thread && running_thread->start_routine) {
        running_thread->start_routine(running_thread->arg);
    }
    uthread_exit(NULL);
}


void scheduler_yield(void) {
    if (running_thread == NULL) return;
    
    if (running_thread->tid == 0) {
        getcontext(&main_context);
    } else {
        getcontext(&running_thread->context);
    }
    
    if (running_thread->state == THREAD_RUNNING) {
        running_thread->state = THREAD_READY;
        enqueue_thread(running_thread);
        scheduler_schedule();
    }
}

void scheduler_schedule(void) {
    // Free pending stack from previous terminated thread
    if (thread_to_free != NULL) {
        if (thread_to_free->stack) {
            free(thread_to_free->stack);
            thread_to_free->stack = NULL;
        }
        thread_to_free = NULL;
    }

    thread_t *next = dequeue_thread();
    
    // If no threads are ready, switch to main thread
    if (next == NULL) {
        if (running_thread && running_thread->tid == 0) {
            return;
        }
        
        thread_t *prev = running_thread;
        running_thread = &threads[0];
        running_thread->state = THREAD_RUNNING;
        
        if (prev->state == THREAD_TERMINATED) {
            thread_to_free = prev;
            setcontext(&main_context);
        } else {
            swapcontext(&prev->context, &main_context);
        }
        return;
    }
    
    thread_t *prev = running_thread;
    running_thread = next;
    running_thread->state = THREAD_RUNNING;
    
    ucontext_t *prev_ctx;
    if (prev == NULL || prev->tid == 0) {
        prev_ctx = &main_context;
    } else {
        prev_ctx = &prev->context;
    }
    
    ucontext_t *next_ctx = (running_thread->tid == 0) ? &main_context : &running_thread->context;

    if (prev && prev->state == THREAD_TERMINATED) {
        thread_to_free = prev;
        setcontext(next_ctx);
    } else {
        swapcontext(prev_ctx, next_ctx);
    }
}

int uthread_self(void) {
    if (running_thread == NULL) {
        return 0;
    }
    return running_thread->tid;
}

void uthread_exit(void *retval) {
    block_signals();

    if (running_thread == NULL || running_thread->tid == 0) {
        exit(0);
    }
    
    running_thread->retval = retval;
    running_thread->state = THREAD_TERMINATED;
    
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].state != THREAD_TERMINATED && threads[i].waiting_for == running_thread) {
            unblock_thread(&threads[i]);
        }
    }
    
    thread_count--;
    
    scheduler_schedule();
    exit(1);
}

int uthread_join(int tid, void **retval) {
    block_signals();

    thread_t *target = find_thread(tid);
    
    if (target == NULL) {
        unblock_signals();
        return -1;
    }
    
    if (target->state == THREAD_TERMINATED) {
        if (retval) {
            *retval = target->retval;
        }
        unblock_signals();
        return 0;
    }
    
    running_thread->state = THREAD_BLOCKED;
    running_thread->waiting_for = target;
    
    thread_t *prev = running_thread;
    if (prev->tid == 0) {
        getcontext(&main_context);
    } else {
        getcontext(&prev->context);
    }
    
    if (running_thread->state == THREAD_BLOCKED && running_thread->waiting_for == target) {
        scheduler_schedule();
    }
    
    if (retval) {
        *retval = target->retval;
    }
    
    unblock_signals();
    return 0;
}

int uthread_mutex_init(mutex_t *mutex) {
    if (mutex == NULL) {
        return -1;
    }
    mutex->locked = 0;
    mutex->owner = NULL;
    mutex->waiting_list = NULL;
    return 0;
}

int uthread_mutex_lock(mutex_t *mutex) {
    block_signals();

    if (mutex == NULL || running_thread == NULL) {
        unblock_signals();
        return -1;
    }
    
    if (mutex->locked == 0) {
        mutex->locked = 1;
        mutex->owner = running_thread;
        unblock_signals();
        return 0;
    }
    
    if (mutex->owner == running_thread) {
        unblock_signals();
        return -1;
    }
    
    running_thread->state = THREAD_BLOCKED;
    running_thread->blocked_on = mutex;
    
    if (mutex->waiting_list == NULL) {
        mutex->waiting_list = running_thread;
        running_thread->next = NULL;
    } else {
        thread_t *current = mutex->waiting_list;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = running_thread;
        running_thread->next = NULL;
    }
    
    thread_t *prev = running_thread;
    if (prev->tid == 0) {
        getcontext(&main_context);
    } else {
        getcontext(&prev->context);
    }
    
    if (running_thread->state == THREAD_BLOCKED) {
        scheduler_schedule();
    }
    
    mutex->locked = 1;
    mutex->owner = running_thread;
    
    unblock_signals();
    return 0;
}

int uthread_mutex_unlock(mutex_t *mutex) {
    block_signals();

    if (mutex == NULL || running_thread == NULL) {
        unblock_signals();
        return -1;
    }
    
    if (mutex->owner != running_thread) {
        unblock_signals();
        return -1;
    }
    
    mutex->locked = 0;
    mutex->owner = NULL;
    
    if (mutex->waiting_list != NULL) {
        thread_t *next = mutex->waiting_list;
        mutex->waiting_list = next->next;
        unblock_thread(next);
    }
    
    unblock_signals();
    return 0;
}

static void safe_print_str(const char *s) {
    write(STDOUT_FILENO, s, strlen(s));
}

static void safe_print_int(int n) {
    char buffer[32];
    int i = 0;
    if (n == 0) {
        buffer[i++] = '0';
    } else {
        if (n < 0) {
            safe_print_str("-");
            n = -n;
        }
        int temp = n;
        int len = 0;
        int temp_len = n;
        while (temp_len > 0) {
            temp_len /= 10;
            len++;
        }
        
        i = len;
        while (temp > 0) {
            buffer[--len] = '0' + (temp % 10);
            temp /= 10;
        }
    }
    write(STDOUT_FILENO, buffer, i);
}

static void print_deadlock_report(void) {
    
    safe_print_str("Deadlock report:\n");

    bool has_deadlock = false;
    
    // Check for cycles in mutex waiting
    for (int i = 0; i < MAX_THREADS; i++) { // Check all slots
        thread_t *t = &threads[i];
        if (t->tid != 0 && t->state == THREAD_BLOCKED && t->blocked_on != NULL) {
            thread_t *start = t;
            thread_t *current = start;
            int depth = 0;
            bool cycle = false;
            
            // Follow the chain
            while (current && current->blocked_on && depth < MAX_THREADS) {
                mutex_t *mutex = current->blocked_on;
                if (mutex->owner == NULL) {
                    break;
                }
                if (mutex->owner == start) {
                    cycle = true;
                    break;
                }
                current = mutex->owner;
                depth++;
            }
            
            if (cycle) {
                if (!has_deadlock) {
                    // Only print header once
                    has_deadlock = true;
                }
                safe_print_str("Deadlock detected! Cycle involving thread ");
                safe_print_int(start->tid);
                safe_print_str("\n");
                
                current = start;
                safe_print_str("  Thread ");
                safe_print_int(current->tid);
                safe_print_str(" -> ");
                
                depth = 0;
                do {
                    if (current->blocked_on && current->blocked_on->owner) {
                        safe_print_str("Thread ");
                        safe_print_int(current->blocked_on->owner->tid);
                        safe_print_str(" -> ");
                        current = current->blocked_on->owner;
                    } else {
                        break;
                    }
                } while (current != start && depth++ < MAX_THREADS);
                safe_print_str("Thread ");
                safe_print_int(start->tid);
                safe_print_str("\n");
                
                break; 
            }
        }
    }
    
    if (!has_deadlock) {
        safe_print_str("No deadlock detected.\n");
    }
}

int uthread_rwlock_init(rwlock_t *rwlock) {
    if (rwlock == NULL) {
        return -1;
    }
    rwlock->readers = 0;
    rwlock->writer = NULL;
    rwlock->read_waiting = NULL;
    rwlock->write_waiting = NULL;
    rwlock->readers_list = NULL;
    return 0;
}

int uthread_rwlock_rdlock(rwlock_t *rwlock) {
    block_signals();

    if (rwlock == NULL || running_thread == NULL) {
        unblock_signals();
        return -1;
    }
    
    // If no writer and no writers waiting, allow read
    if (rwlock->writer == NULL && rwlock->write_waiting == NULL) {
        rwlock->readers++;
        if (rwlock->readers_list == NULL) {
            rwlock->readers_list = running_thread;
            running_thread->next = NULL;
        } else {
            thread_t *current = rwlock->readers_list;
            while (current->next != NULL) {
                current = current->next;
            }
            current->next = running_thread;
            running_thread->next = NULL;
        }
        unblock_signals();
        return 0;
    }
    
    running_thread->state = THREAD_BLOCKED;
    running_thread->blocked_on_rw = rwlock;
    running_thread->is_writer = false;
    
    // Add to read waiting list
    if (rwlock->read_waiting == NULL) {
        rwlock->read_waiting = running_thread;
        running_thread->next = NULL;
    } else {
        thread_t *current = rwlock->read_waiting;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = running_thread;
        running_thread->next = NULL;
    }
    
    // Context switch
    thread_t *prev = running_thread;
    if (prev->tid == 0) {
        getcontext(&main_context);
    } else {
        getcontext(&prev->context);
    }
    
    if (running_thread->state == THREAD_BLOCKED) {
        scheduler_schedule();
    }
    
    // When we resume, we should have the read lock
    rwlock->readers++;
    if (rwlock->readers_list == NULL) {
        rwlock->readers_list = running_thread;
        running_thread->next = NULL;
    } else {
        thread_t *current = rwlock->readers_list;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = running_thread;
        running_thread->next = NULL;
    }
    
    unblock_signals();
    return 0;
}

int uthread_rwlock_wrlock(rwlock_t *rwlock) {
    block_signals();

    if (rwlock == NULL || running_thread == NULL) {
        unblock_signals();
        return -1;
    }
    
    // If no readers and no writer, allow write
    if (rwlock->readers == 0 && rwlock->writer == NULL) {
        rwlock->writer = running_thread;
        unblock_signals();
        return 0;
    }
    
    // Block and wait
    running_thread->state = THREAD_BLOCKED;
    running_thread->blocked_on_rw = rwlock;
    running_thread->is_writer = true;
    
    // Add to write waiting list
    if (rwlock->write_waiting == NULL) {
        rwlock->write_waiting = running_thread;
        running_thread->next = NULL;
    } else {
        thread_t *current = rwlock->write_waiting;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = running_thread;
        running_thread->next = NULL;
    }
    
    // Context switch
    thread_t *prev = running_thread;
    if (prev->tid == 0) {
        getcontext(&main_context);
    } else {
        getcontext(&prev->context);
    }
    
    if (running_thread->state == THREAD_BLOCKED) {
        scheduler_schedule();
    }
    
    // When we resume, we should have the write lock
    rwlock->writer = running_thread;
    
    unblock_signals();
    return 0;
}

// Read-write lock unlock
int uthread_rwlock_unlock(rwlock_t *rwlock) {
    block_signals();

    if (rwlock == NULL || running_thread == NULL) {
        unblock_signals();
        return -1;
    }
    
    if (rwlock->writer == running_thread) {
        // Unlocking write lock
        rwlock->writer = NULL;
        
        // Unblock waiting readers (all of them can proceed)
        while (rwlock->read_waiting != NULL) {
            thread_t *next = rwlock->read_waiting;
            rwlock->read_waiting = next->next;
            unblock_thread(next);
        }
        
        // If no readers waiting, unblock first writer
        if (rwlock->read_waiting == NULL && rwlock->write_waiting != NULL) {
            thread_t *next = rwlock->write_waiting;
            rwlock->write_waiting = next->next;
            unblock_thread(next);
        }
    } else {
        // Check if thread is a reader
        thread_t *reader = rwlock->readers_list;
        thread_t *prev_reader = NULL;
        bool is_reader = false;
        
        while (reader != NULL) {
            if (reader == running_thread) {
                is_reader = true;
                // Remove from readers list
                if (prev_reader == NULL) {
                    rwlock->readers_list = reader->next;
                } else {
                    prev_reader->next = reader->next;
                }
                reader->next = NULL;
                break;
            }
            prev_reader = reader;
            reader = reader->next;
        }
        
        if (is_reader) {
            // Unlocking read lock
            rwlock->readers--;
            
            // If last reader, unblock waiting writers
            if (rwlock->readers == 0 && rwlock->write_waiting != NULL) {
                thread_t *next = rwlock->write_waiting;
                rwlock->write_waiting = next->next;
                unblock_thread(next);
            }
        } else {
            unblock_signals();
            return -1; 
        }
    }
    
    unblock_signals();
    return 0;
}

int uthread_rwlock_destroy(rwlock_t *rwlock) {
    if (rwlock == NULL) {
        return -1;
    }
    
    if (rwlock->readers > 0 || rwlock->writer != NULL) {
        return -1; // Lock is still in use
    }
    rwlock->readers = 0;
    rwlock->writer = NULL;
    rwlock->read_waiting = NULL;
    rwlock->write_waiting = NULL;
    rwlock->readers_list = NULL;
    
    return 0;
}

void deadlock_detect(void) {
    print_deadlock_report();
}