#!/bin/bash
./test_deadlock > output.log 2>&1 &
PID=$!
sleep 2
kill -QUIT $PID
sleep 1
cat output.log
rm output.log
kill -9 $PID 2>/dev/null
