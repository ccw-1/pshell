#!/bin/bash
# Simple test - what does the shell actually send?
stty -echo
while IFS= read -r -n1 char; do
    printf "Received: %d (0x%02x) '%s'\n" "'$char" "'$char" "$char" >> /tmp/shell_input.log
    if [ "$char" = $'\n' ]; then
        echo "Got newline" >> /tmp/shell_input.log
        break
    fi
done
stty echo
cat /tmp/shell_input.log
