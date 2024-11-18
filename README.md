A small shell made in C for my operating systems class as an exercise to learn how forking and processes work. Reads user input and parses it into command and arguments. Implements built-in commands `cd`, `status` and `exit,` all other commands are executed by forking the process and replacing the child using the built in Unix command `exec`. Also supports input and output redirection, running processes in the background, and reading the shell's pid. 

To compile:
```
gcc -std=gnu99 -o smallsh smallsh.c
```

To run:
```
./smallsh
```
