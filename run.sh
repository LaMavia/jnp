#!/usr/bin/env bash

printf "compiling...\n"
g++ -Wall -Wextra -O2 -g -std=c++20 top7.cc -o top7 || exit 1

if [ "$1" != 'debug' ]; then
    printf "setting permissions...\n" # ojalá
    chmod +x top7 || exit 1

    printf "running...\n"
    ./top7
fi
