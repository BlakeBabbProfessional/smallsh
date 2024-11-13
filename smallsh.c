#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/fcntl.h>

#include "smallsh.h"

#define MAX_LENGTH 2048

// Control program flow
enum state {
	Get,
	BuiltIn,
	Exec,
	Clean,
	Exit
};

enum state state = Get;
int status = 0;
int foreground_pid = 0;
int background_disabled = 0;

/*
 * A tiny shell!
 */
int main() {
	// Create SIGINT handler
	struct sigaction SIGINT_action = {0};
	SIGINT_action.sa_handler = catch_SIGINT;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);

	// Create SIGTSTP handler
	struct sigaction SIGTSTP_action = {0};
	SIGTSTP_action.sa_handler = catch_SIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = 0;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	struct line* line = malloc(sizeof(struct line));
	struct processes* head = malloc(sizeof(struct processes));

	while (1) {
		switch (state) {
			case Get:
				get(line);
				break;
			case BuiltIn:
				builtin(line);
				break;
			case Exec:
				head = exec(line, head);
				break;
			case Clean:
				clean(head);
				break;
			case Exit:
				goto Exit;
		}
	}
	Exit:

	// Kill all child processes
	if (!head->next) return 0;
	struct processes* curr = head->next;
	while (curr) {
		printf("Ending process [%d]\n", curr->pid);
		fflush(stdout);
		kill(curr->pid, SIGINT);
		curr = curr->next;
	}

	return 0;
}

/*
 * Gets input from the user and stores it in a struct.
 *
 * Params:
 * 	line - the struct to store the input in
 */
void get(struct line* line) {
	printf(": ");
	fflush(stdout);

	// Get user input, start over if bad read
	char line_str[MAX_LENGTH];
	memset(line_str, '\0', MAX_LENGTH);
	if (!fgets(line_str, MAX_LENGTH, stdin)) {
		state = Clean;
		return;
	}

	// Expand $$
	while (strstr(line_str, "$$")) {
		// Replace $$ with %d so I can sprintf
		for (int i = 0; i < MAX_LENGTH; i++) {
			if (line_str[i] == '$' && line_str[i+1] == '$') {
				line_str[i] = '%';
				line_str[i+1] = 'd';
				break;
			}
		}

		char buffer[MAX_LENGTH];
		sprintf(buffer, line_str, getpid());
		strncpy(line_str, buffer, MAX_LENGTH);
	}

	char* delim = " ";
	char* delim_exp = " \n";

	line->command = malloc(sizeof(char) * MAX_LENGTH);
	line->input = malloc(sizeof(char) * MAX_LENGTH);
	line->output = malloc(sizeof(char) * MAX_LENGTH);

	line->background = 0;
	line->arg_count = 0;

	if (line_str[0] == '#' || line_str[0] == '\n') {
		state = Clean;
		return;
	}
	state = BuiltIn;

	// Command, remove trailing \n
	char* command = strtok(line_str, delim);
	if (strstr(command, "\n")) {
		line->command = strncpy(line->command, command, strlen(command)-1);
	} else {
		line->command = strcpy(line->command, command);
	}

	// Find arg number
	int arg_count = 0;
	//(I'm going to do this by just iterating through the array)
	for (int i = 0; ; i++) {
		char elem = line_str[i];
		if (elem == ' ' || elem == '\0') arg_count++;
		if (elem == '\n') break;
		if (elem == '>' || elem == '<' || elem == '&') {
			arg_count--;
			break;
		}
	}
	if (arg_count > 512) {
		printf("Too many arguments\n");
		fflush(stdout);
		return;
	}
	line->arg_count = arg_count;
	// Add args
	for (int i = 0; i < arg_count; i++) {
		line->args[i] = malloc(sizeof(char) * MAX_LENGTH);
		line->args[i] = strcpy(line->args[i], strtok(NULL, delim_exp));
	}

	// Remaining stuff
	while (1) {
		char* next = strtok(NULL, delim);
		if (!next) return;

		// Input
		if (strstr(next, "<")) {
			line->input = strcpy(line->input, strtok(NULL, delim_exp));
		}

		// Output
		if (strstr(next, ">")) {
			line->output = strcpy(line->output, strtok(NULL, delim_exp));
		}

		// background
		if (strstr(next, "&")) {
			line->background = 1;
			return;
		}
	}
}

/*
 * Executes the exit command, terminating the shell.
 *
 * Params:
 * 	line - a struct representing the line the user entered
 */
void execute_exit(struct line* line) {
	if (line->arg_count == 0) {
		state = Exit;
	} else {
		printf("exit takes 0 arguments\n");
		fflush(stdout);
		state = Clean;
	}
}

/*
 * Executes the cd command, changing the shell's working directory.
 * Directory is changed to argument if provided, elso go to the home.
 *
 * Params:
 * 	line - a struct representing the line the user entered
 */
void execute_cd(struct line* line) {
	if (line->arg_count == 0) {
		int r = chdir(getenv("HOME"));
		if (r == -1) {
			printf("cd failed\n");
			fflush(stdout);
		}
	} else if (line->arg_count == 1) {
		int r = chdir(line->args[0]);
		if (r == -1) {
			printf("cd failed\n");
			fflush(stdout);
		}
	} else {
		printf("cd takes 0 or 1 arguments\n");
		fflush(stdout);
	}
	state = Clean;
}

/*
 * Executes the status command, printing the exit status of the last command.
 *
 * Params:
 * 	line - a struct representing the line the user entered
 */
void execute_status(struct line* line) {
	if (line->arg_count == 0) {
		switch (status) {
			case 0:
			case 1:
				printf("Exit status %d\n", status);
				fflush(stdout);
				break;
			// idk
			case 256:
				printf("Exit status 1\n");
				fflush(stdout);
				break;
			default:
				printf("Terminated by signal %d\n", status);
				fflush(stdout);
				break;
		}
	} else {
		printf("status takes 0 arguments\n");
		fflush(stdout);
	}
	state = Clean;
}

/*
 * Checks if the user entered a built-in command, executes if so.
 *
 * Params:
 * 	line - a struct representing the line the user entered
 */
void builtin(struct line* line) {
	// Detect exit
	if (strcmp(line->command, "exit") == 0) {
		execute_exit(line);
		return;
	}

	// Detect cd
	if (strcmp(line->command, "cd") == 0) {
		execute_cd(line);
		return;
	}

	// Detect status
	if (strcmp(line->command, "status") == 0) {
		execute_status(line);
		return;
	}

	state = Exec;
}

/*
 * Uses exec to execute other commands, passes arguments and controls background
 * and foreground.
 *
 * Params:
 * 	line - a struct representing the line the user entered
 * Return:
 * 	Pointer to new head of processes ll
 */
struct processes* exec(struct line* line, struct processes* head) {
	pid_t spawn_pid = fork();
	switch (spawn_pid) {
		// Error
		case -1:
			perror("Fork error\n");
			state = Exit;
			break;
		// Child
		case 0: {
			// Create signal handler
			if (line->background) {
				struct sigaction ignore_action = {0};
				ignore_action.sa_handler = SIG_IGN;
				sigaction(SIGINT, &ignore_action, NULL);
				sigaction(SIGTSTP, &ignore_action, NULL);
			}

			// Get args
			char* args[line->arg_count + 2];
			args[0] = malloc(sizeof(char) * MAX_LENGTH);
			args[0] = strcpy(args[0], line->command);
			for (int i = 0; i < line->arg_count; i++) {
				args[i+1] = malloc(sizeof(char) * MAX_LENGTH);
				args[i+1] = strcpy(args[i+1], line->args[i]);
			}
			args[line->arg_count+1] = NULL;

			// Redirect input and output
			if (strcmp(line->output, "") != 0) {
				int output = open(line->output, O_WRONLY | O_CREAT | O_TRUNC, 0633);
				dup2(output, 1);
			} else if (line->background && !background_disabled) {
				int dev_null = open("/dev/null", O_WRONLY);
				dup2(dev_null, 1);
			}
			if (strcmp(line->input, "") != 0) {
				int input = open(line->input, O_RDONLY | O_CREAT, 0633);
				dup2(input, 0);
			} else if (line->background && !background_disabled) {
				int dev_null = open("/dev/null", O_RDONLY);
				dup2(dev_null, 0);
			}

			execvp(line->command, args);

			// exec failed
			// TODO: exit status
			for (int i = 0; i < line->arg_count+1; i++) {
				free(args[i]);
			}
			printf("Command not found\n");
			fflush(stdout);
			exit(1);
		}
		// Parent
		default:
			// Process in background
			if (line->background && !background_disabled) {
				head->pid = spawn_pid;
				struct processes* new_head = malloc(sizeof(struct processes));
				new_head->next = head;
				head = new_head;
				printf("Started up [%d]\n", spawn_pid);
				fflush(stdout);
			// Process in foreground
			} else {
				foreground_pid = spawn_pid;
				waitpid(spawn_pid, &status, 0);
				foreground_pid = 0;
			}
			state = Clean;
	}
	return head;
}

/*
 * Cleans up any completed child processes.
 *
 * Params:
 * 	head - the head of a linked list of all active processes
 */
void clean(struct processes* head) {
	if (!head->next) {
		state = Get;
		return;
	}
	struct processes* prev = head;
	struct processes* curr = head->next;
    while (curr) {
		int exit_method;
		int child_pid = waitpid(curr->pid, &exit_method, WNOHANG);
		if (child_pid) {
			prev->next = curr->next;
			struct processes* old = curr;
			curr = curr->next;
			free(old);
			if (exit_method == 0 || exit_method == 1) {
				printf("Process [%d] ended with status (%d)\n", child_pid, exit_method);
			} else {
				printf("Process [%d] terminated by signal (%d)\n", child_pid, exit_method);
			} fflush(stdout);
		} else {
			prev = curr;
			curr = curr->next;
		}
	}

	state = Get;
}

/*
 * Handler for SIGINT, says foreground disabled if one exist.
 */
void catch_SIGINT(int signo) {
	if (foreground_pid) {
		write(STDOUT_FILENO, " Terminated by signal 2", 23);
		status = 2;
	}
	write(STDOUT_FILENO, "\n", 1);
	foreground_pid = 0;
}

/*
 * Handler for SIGTSTP, toggles foreground-only mode
 */
void catch_SIGTSTP(int signo) {
	if (background_disabled) {
		background_disabled = 0;
		write(STDOUT_FILENO, " Exiting foreground-only mode\n", 30);
	} else {
		background_disabled = 1;
		write(STDOUT_FILENO, " Entering foreground-only mode\n", 31);
	}
}
