// Components of a user-entered line 
struct line	{
	char* command;
	char* input;
	char* output;
	int background;
	int arg_count;
	char* args[];
};

/*
 * Linked list for active processes
 * The head is just a placeholder, it doesn't represent an actual process
 */
struct processes {
	pid_t pid;
	struct processes* next;
};

void get(struct line* line);

void builtin(struct line* line);

struct processes* exec(struct line* line, struct processes* head);

void clean(struct processes* head);

void catch_SIGINT(int signo);

void catch_SIGTSTP(int signo);
