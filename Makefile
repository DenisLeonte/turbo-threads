CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
LDFLAGS = -pthread

# Library files
LIB_SRC = uthread.c
LIB_OBJ = $(LIB_SRC:.c=.o)
LIB = libuthread.a

# Test programs
TESTS = test_basic test_mutex test_rwlock test_deadlock

.PHONY: all clean test

all: $(LIB)

# Build the library
$(LIB): $(LIB_OBJ)
	ar rcs $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Test programs
test_basic: test_basic.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< -L. -luthread

test_mutex: test_mutex.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< -L. -luthread

test_rwlock: test_rwlock.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< -L. -luthread

test_deadlock: test_deadlock.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< -L. -luthread

test: $(TESTS)
	@echo "Running basic test..."
	./test_basic
	@echo "\nRunning mutex test..."
	./test_mutex
	@echo "\nRunning rwlock test..."
	./test_rwlock
	@echo "\nRunning deadlock test (send SIGQUIT with Ctrl+\\)..."
	./test_deadlock

clean:
	rm -f $(LIB) $(LIB_OBJ) $(TESTS)
