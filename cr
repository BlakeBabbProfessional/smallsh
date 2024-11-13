#!/bin/bash

if gcc -std=gnu99 -o smallsh smallsh.c
then
	chmod 700 smallsh
	./smallsh
else
	echo "Compilation failed"
	exit 1
fi
