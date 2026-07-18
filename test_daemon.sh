#!/bin/bash
pkill -9 -f "bin/pshell" 2>/dev/null
sleep 1
timeout 5 strace -e trace=select,poll,epoll_wait -f ./bin/pshell -f -c config/pshell-dev.conf 2>&1 | grep -A5 "Entering main"
