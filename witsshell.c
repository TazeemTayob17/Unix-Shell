#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

/* Single-line error message required by spec */
static void print_error(void)
{
	const char msg[] = "An error has occurred\n";
	(void)write(STDERR_FILENO, msg, sizeof(msg) - 1);
}

/* PATH list management */
static char **pathv = NULL;
static size_t pathc = 0;

static void free_pathv(void)
{
	if (!pathv)
		return;
	for (size_t i = 0; i < pathc; ++i)
		free(pathv[i]);
	free(pathv);
	pathv = NULL;
	pathc = 0;
}

static void set_path(char **dirs, int ndirs)
{
	free_pathv();
	if (ndirs <= 0)
	{
		pathv = NULL;
		pathc = 0;
		return;
	}
	pathv = calloc((size_t)ndirs, sizeof(char *));
	if (!pathv)
	{
		print_error();
		return;
	}
	pathc = (size_t)ndirs;
	for (int i = 0; i < ndirs; ++i)
	{
		pathv[i] = strdup(dirs[i]);
		if (!pathv[i])
			print_error();
	}
}

static void init_default_path(void)
{
	char *init[] = {"/bin"};
	set_path(init, 1);
}

static char *join_dir_cmd(const char *dir, const char *cmd)
{
	size_t ld = strlen(dir), lc = strlen(cmd);
	char *s = malloc(ld + 1 + lc + 1);
	if (!s)
		return NULL;
	memcpy(s, dir, ld);
	s[ld] = '/';
	memcpy(s + ld + 1, cmd, lc);
	s[ld + 1 + lc] = '\0';
	return s;
}

static char *resolve_command_path(const char *argv0)
{
	if (!argv0 || !*argv0)
		return NULL;
	if (strchr(argv0, '/'))
	{ /* direct path */
		if (access(argv0, X_OK) == 0)
			return strdup(argv0);
		return NULL;
	}
	if (!pathv || pathc == 0)
		return NULL;
	for (size_t i = 0; i < pathc; ++i)
	{
		char *cand = join_dir_cmd(pathv[i], argv0);
		if (!cand)
		{
			print_error();
			return NULL;
		}
		if (access(cand, X_OK) == 0)
			return cand;
		free(cand);
	}
	return NULL;
}

/* Normalize operators '>' and '&' by ensuring spaces around them so that
   tokenization works even when operators are glued to words (e.g., ls>out). */
static char *normalize_ops(const char *line)
{
	if (!line)
		return NULL;
	size_t n = strlen(line);
	size_t cap = n * 3 + 1;
	char *out = malloc(cap);
	if (!out)
	{
		print_error();
		return NULL;
	}
	size_t j = 0;
	for (size_t i = 0; i < n; ++i)
	{
		char c = line[i];
		if (c == '>' || c == '&')
		{
			if (j > 0 && out[j - 1] != ' ' && out[j - 1] != '\t')
				out[j++] = ' ';
			out[j++] = c;
			if (i + 1 < n)
			{
				char nx = line[i + 1];
				if (nx != ' ' && nx != '\t' && nx != '\n' && nx != '\r')
					out[j++] = ' ';
			}
		}
		else
		{
			out[j++] = c;
		}
		if (j + 4 >= cap)
		{
			cap *= 2;
			char *tmp = realloc(out, cap);
			if (!tmp)
			{
				free(out);
				print_error();
				return NULL;
			}
			out = tmp;
		}
	}
	out[j] = '\0';
	return out;
}

/* Tokenize by whitespace using strsep, skipping empty tokens */
static int tokenize(char *line, char **argv, int maxv)
{
	int argc = 0;
	char *s = line;
	char *tok;
	while ((tok = strsep(&s, " \t\r\n")) != NULL)
	{
		if (*tok == '\0')
			continue;
		if (argc == maxv - 1)
			break;
		argv[argc++] = tok;
	}
	argv[argc] = NULL;
	return argc;
}

/* Parse a simple command for tokens and optional redirection > file.
   Returns:
	 >=0 : argc (argv_out filled, argv_out[argc]=NULL), redir_path set or NULL
	  -1 : syntax error (prints error)
*/
static int parse_simple_command(char *cmdline, char **argv_out, int maxv, char **redir_path)
{
	*redir_path = NULL;
	char *toks[256];
	int nt = 0;
	char *s = cmdline, *tok;
	while ((tok = strsep(&s, " \t\r\n")) != NULL)
	{
		if (*tok == '\0')
			continue;
		if (nt >= (int)(sizeof(toks) / sizeof(toks[0])) - 1)
			break;
		toks[nt++] = tok;
	}
	if (nt == 0)
		return 0;
	int gt = -1;
	for (int i = 0; i < nt; ++i)
	{
		if (strcmp(toks[i], ">") == 0)
		{
			if (gt != -1)
			{
				print_error();
				return -1;
			}
			gt = i;
		}
	}
	int argc = 0;
	if (gt == -1)
	{
		for (int i = 0; i < nt && argc < maxv - 1; ++i)
			argv_out[argc++] = toks[i];
		argv_out[argc] = NULL;
		return argc;
	}
	/* redirection present: must have one filename and nothing after */
	if (gt == 0)
	{
		print_error();
		return -1;
	}
	if (gt == nt - 1)
	{
		print_error();
		return -1;
	}
	if (gt + 2 != nt)
	{
		print_error();
		return -1;
	}
	for (int i = 0; i < gt && argc < maxv - 1; ++i)
		argv_out[argc++] = toks[i];
	argv_out[argc] = NULL;
	*redir_path = toks[gt + 1];
	return argc;
}

/* Exec helper (called in child). Apply redirection if requested. */
static void exec_external(char *resolved, char **argv, const char *redir)
{
	if (redir)
	{
		int fd = open(redir, O_CREAT | O_TRUNC | O_WRONLY, 0666);
		if (fd < 0)
		{
			print_error();
			_exit(1);
		}
		if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0)
		{
			print_error();
			close(fd);
			_exit(1);
		}
		close(fd);
	}
	execv(resolved, argv);
	print_error();
	_exit(1);
}

/* Execute one command segment (no '&'). If an external child is spawned, *spawned_pid set >0.
   If the built-in exit was properly used, *want_exit set to 1. */
static int execute_segment(char *segment, pid_t *spawned_pid, int *want_exit)
{
	*spawned_pid = 0;
	*want_exit = 0;
	const int MAXV = 128;
	char *argv[MAXV];
	char *redir = NULL;
	int argc = parse_simple_command(segment, argv, MAXV, &redir);
	if (argc < 0)
		return -1;
	if (argc == 0)
		return 0;

	if (strcmp(argv[0], "exit") == 0)
	{
		if (redir != NULL || argc != 1)
		{
			print_error();
			return 0;
		}
		*want_exit = 1;
		return 0;
	}
	if (strcmp(argv[0], "cd") == 0)
	{
		if (redir != NULL || argc != 2)
		{
			print_error();
			return 0;
		}
		if (chdir(argv[1]) != 0)
			print_error();
		return 0;
	}
	if (strcmp(argv[0], "path") == 0)
	{
		if (redir != NULL)
		{
			print_error();
			return 0;
		}
		set_path(&argv[1], argc - 1);
		return 0;
	}

	/* External */
	if (!pathv || pathc == 0)
	{
		print_error();
		return 0;
	}
	char *resolved = resolve_command_path(argv[0]);
	if (!resolved)
	{
		print_error();
		return 0;
	}

	pid_t pid = fork();
	if (pid < 0)
	{
		print_error();
		free(resolved);
		return 0;
	}
	if (pid == 0)
	{
		exec_external(resolved, argv, redir);
		/* not reached */
	}
	else
	{
		*spawned_pid = pid;
		free(resolved);
		return 0;
	}
	return 0;
}

/* Execute a normalized line which may include '&' separated segments.
   Launch all external children first, then wait for them. Return 1 if exit was requested. */
static int execute_normalized_line(char *norm)
{
	pid_t pids[256];
	int np = 0;
	int want_exit = 0;
	char *s = norm;
	char *seg;
	while ((seg = strsep(&s, "&")) != NULL)
	{
		/* trim leading/trailing whitespace */
		while (*seg == ' ' || *seg == '\t' || *seg == '\r' || *seg == '\n')
			seg++;
		int L = (int)strlen(seg);
		while (L > 0 && (seg[L - 1] == ' ' || seg[L - 1] == '\t' || seg[L - 1] == '\r' || seg[L - 1] == '\n'))
			seg[--L] = '\0';
		if (*seg == '\0')
			continue;
		char *cpy = strdup(seg);
		if (!cpy)
		{
			print_error();
			continue;
		}
		pid_t child = 0;
		int w = 0;
		(void)execute_segment(cpy, &child, &w);
		if (child > 0 && np < (int)(sizeof(pids) / sizeof(pids[0])))
			pids[np++] = child;
		if (w)
			want_exit = 1;
		free(cpy);
	}
	for (int i = 0; i < np; ++i)
		waitpid(pids[i], NULL, 0);
	return want_exit;
}

static void process_stream(FILE *in, int show_prompt)
{
	char *line = NULL;
	size_t cap = 0;
	while (1)
	{
		if (show_prompt)
		{
			fputs("witsshell> ", stdout);
			fflush(stdout);
		}
		ssize_t n = getline(&line, &cap, in);
		if (n < 0)
			break;
		if (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[n - 1] = '\0';
		char *norm = normalize_ops(line);
		if (!norm)
			break;
		int quit = execute_normalized_line(norm);
		free(norm);
		if (quit)
			break;
	}
	free(line);
}

int main(int argc, char *argv[])
{
	init_default_path();
	if (argc == 1)
	{
		process_stream(stdin, 1);
		free_pathv();
		return 0;
	}
	else if (argc == 2)
	{
		FILE *fp = fopen(argv[1], "r");
		if (!fp)
		{
			print_error();
			free_pathv();
			return 1;
		}
		process_stream(fp, 0);
		fclose(fp);
		free_pathv();
		return 0;
	}
	else
	{
		print_error();
		free_pathv();
		return 1;
	}
}
