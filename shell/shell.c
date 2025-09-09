// shell/shell.c
// Minimal educational shell: cmd exec, cd/exit builtins, I/O redirection, single pipe, background (&)


#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>


#define MAX_TOKENS 256
#define MAX_LINE 4096


static void sigint_handler(int sig) {
(void)sig;
write(STDOUT_FILENO, "\n", 1); // move to new line on Ctrl+C
}


static char* trim(char* s) {
if (!s) return s;
size_t len = strlen(s);
size_t i = 0; while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) i++;
size_t j = len; while (j > i && (s[j-1] == ' ' || s[j-1] == '\t' || s[j-1] == '\n')) j--;
if (i > 0) memmove(s, s + i, j - i);
s[j - i] = '\0';
return s;
}


static int tokenize(char* line, char* tokens[], int max_tokens) {
int count = 0;
char* saveptr = NULL;
for (char* p = strtok_r(line, " \t\n", &saveptr); p && count < max_tokens - 1; p = strtok_r(NULL, " \t\n", &saveptr)) {
tokens[count++] = p;
}
tokens[count] = NULL;
return count;
}
static void parse_redirs(char* tokens[], char* argv[], char** in_file, char** out_file, int* append) {
*in_file = NULL; *out_file = NULL; *append = 0;
int ai = 0;
for (int i = 0; tokens[i]; ++i) {
if (strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], ">>") == 0) {
*append = (tokens[i][1] == '>');
if (!tokens[i+1]) { fprintf(stderr, "syntax error: expected filename after '%s'\n", tokens[i]); return; }
*out_file = tokens[i+1];
i++; // skip filename
} else if (strcmp(tokens[i], "<") == 0) {
if (!tokens[i+1]) { fprintf(stderr, "syntax error: expected filename after '<'\n"); return; }
*in_file = tokens[i+1];
i++;
} else if (strcmp(tokens[i], "&") == 0) {
// handled by caller (background)
continue;
} else {
argv[ai++] = tokens[i];
}
}
argv[ai] = NULL;
}

static int is_background(char* tokens[]) {
int n = 0; while (tokens[n]) n++;
if (n > 0 && strcmp(tokens[n-1], "&") == 0) { tokens[n-1] = NULL; return 1; }
return 0;
}


static int builtin_cd(char* path) {
if (!path) path = getenv("HOME");
if (!path) path = "/";
if (chdir(path) != 0) { perror("cd"); return -1; }
return 0;
}

static void exec_with_redirs(char* argv[], char* in_file, char* out_file, int append) {
if (in_file) {
int fd = open(in_file, O_RDONLY);
if (fd < 0) { perror("open <"); _exit(1); }
if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2 <"); _exit(1); }
close(fd);
}
if (out_file) {
int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
int fd = open(out_file, flags, 0644);
if (fd < 0) { perror("open >"); _exit(1); }
if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2 >"); _exit(1); }
close(fd);
}
execvp(argv[0], argv);
perror("execvp");
_exit(127);
}


static void run_single(char* tokens[]) {
int bg = is_background(tokens);
char* argv[MAX_TOKENS]; char *in_file, *out_file; int append;
parse_redirs(tokens, argv, &in_file, &out_file, &append);
if (!argv[0]) return; // empty after redir removal


// Built-ins
if (strcmp(argv[0], "exit") == 0) exit(0);
if (strcmp(argv[0], "cd") == 0) { builtin_cd(argv[1]); return; }


pid_t pid = fork();
if (pid < 0) { perror("fork"); return; }
if (pid == 0) {
// child: default SIGINT
signal(SIGINT, SIG_DFL);
exec_with_redirs(argv, in_file, out_file, append);
} else {
if (!bg) {
int status; (void)waitpid(pid, &status, 0);
} else {
printf("[bg] pid=%d\n", pid);
}
}
}

static void run_pipeline(char* left_tokens[], char* right_tokens[]) {
	int pipefd[2];
	pid_t p1, p2;
	char *left_argv[MAX_TOKENS], *right_argv[MAX_TOKENS];
	char *inL, *outL, *inR, *outR;
	int appL, appR;
	int bg = 0;

	// Parse redirections for left and right commands
	parse_redirs(left_tokens, left_argv, &inL, &outL, &appL);
	parse_redirs(right_tokens, right_argv, &inR, &outR, &appR);

	// Check for background execution
	bg = is_background(right_tokens);

	if (pipe(pipefd) < 0) { perror("pipe"); return; }

	p1 = fork();
	if (p1 < 0) { perror("fork"); return; }
	if (p1 == 0) {
		// left child
		signal(SIGINT, SIG_DFL);
		close(pipefd[0]); // close read end
		if (dup2(pipefd[1], STDOUT_FILENO) < 0) { perror("dup2 pipe left"); _exit(1); }
		close(pipefd[1]);
		exec_with_redirs(left_argv, inL, outL, appL);
	}

	p2 = fork();
	if (p2 < 0) { perror("fork"); return; }
	if (p2 == 0) {
		// right child
		signal(SIGINT, SIG_DFL);
		close(pipefd[1]); // close write end
		if (dup2(pipefd[0], STDIN_FILENO) < 0) { perror("dup2 pipe right"); _exit(1); }
		close(pipefd[0]);
		exec_with_redirs(right_argv, inR, outR, appR);
	}

	close(pipefd[0]); close(pipefd[1]);
	if (!bg) {
		int st; waitpid(p1, &st, 0); waitpid(p2, &st, 0);
	} else {
		printf("[bg] p1=%d p2=%d\n", p1, p2);
	}
}

int main(void) {
// Ignore SIGINT in shell; children reset to default
signal(SIGINT, sigint_handler);


char *line = NULL; size_t cap = 0;
while (1) {
// prompt: show cwd
char cwd[1024]; if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "?");
printf("myshell:%s$ ", cwd); fflush(stdout);


ssize_t n = getline(&line, &cap, stdin);
if (n < 0) { printf("\n"); break; } // EOF/Ctrl+D
trim(line);
if (line[0] == '\0') continue;


// tokenize
char *tokens[MAX_TOKENS];
int count = tokenize(line, tokens, MAX_TOKENS);
if (count == 0) continue;


// Split on single '|'
int pipe_idx = -1;
for (int i = 0; tokens[i]; ++i) if (strcmp(tokens[i], "|") == 0) { pipe_idx = i; break; }


if (pipe_idx >= 0) {
tokens[pipe_idx] = NULL;
run_pipeline(tokens, &tokens[pipe_idx+1]);
} else {
run_single(tokens);
}
}
free(line);
return 0;
}
