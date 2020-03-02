/**
 * @file yashd.c
 *
 * @brief YASH shell.
 *
 * @author:	Jose Carlos Martinez Garcia-Vaso <carlosgvaso@gmail.com>
 */

#include "yashd.h"


/**
 * @brief Check if a string contains only number characters
 *
 * @param	number	String to check
 * @return	True if the string contains only numbers, false otherwise
 */
bool isNumber(char number[]) {
	int i = 0;

	// Checking for negative numbers
	if (number[0] == '-')
		i = 1;
	for (; number[i] != 0; i++) {
		//if (number[i] > '9' || number[i] < '0')
		if (!isdigit(number[i]))
			return false;
	}
	return true;
}


/**
 * @brief Parse the command line arguments
 *
 * @param	argc	Number of command line arguments
 * @param	argv	Array of command line arguments
 * @return	Struct with the parsed arguments
 */
Arg parseArgs(int argc, char** argv) {
	const char USAGE[MAX_ERROR_LEN] = "\nUsage:\n"
				"./yashd [options]\n"
				"\n"
				"Options:\n"
				"    -h, --help              Print help and exit\n"
				"    -p PORT, --port PORT    Server port [1024-65535]\n"
				"    -v, --verbose           Verbose logger output\n";
		const char ARG_ERROR[MAX_ERROR_LEN] = "-yashd: unknown argument: %s\n";
		const char H_FLAG_SHORT[3] = "-h\0";
		const char H_FLAG_LONG[10] = "--help\0";
		const char P_FLAG_SHORT[3] = "-p\0";
		const char P_FLAG_LONG[10] = "--port\0";
		const char P_INFO[MAX_ERROR_LEN] = "-yashd: using port: %d\n";
		const char P_ERROR1[MAX_ERROR_LEN] = "-yashd: missing port number\n";
		const char P_ERROR2[MAX_ERROR_LEN] = "-yashd: port must be an integer "
				"between %d and %d\n";
		const char V_FLAG_SHORT[3] = "-v\0";
		const char V_FLAG_LONG[10] = "--verbose\0";
		const char V_INFO[MAX_ERROR_LEN] = "-yashd: verbose output enabled\n";
		Arg args = {false, DAEMON_PORT};

	// Loop over the arguments, skipping the command token
	for (int i=1; i<argc; i++) {
		if (!strcmp(H_FLAG_SHORT, argv[i])
				|| !strcmp(H_FLAG_LONG, argv[i])) {
			printf(USAGE);
			exit(EXIT_OK);
		} else if (!strcmp(V_FLAG_SHORT, argv[i])
				|| !strcmp(V_FLAG_LONG, argv[i])) {
			args.verbose = true;
			printf(V_INFO);
		} else if (!strcmp(P_FLAG_SHORT, argv[i])
				|| !strcmp(P_FLAG_LONG, argv[i])) {
			// Port argument detected, next argument should be the port number
			if (i+1 >= argc) {
				printf(P_ERROR1);
				printf(USAGE);
				exit(EXIT_ERR_ARG);
			} else if (!isNumber(argv[i+1])) {
				printf(P_ERROR2, TCP_PORT_LOWER_LIM, TCP_PORT_HIGHER_LIM);
				printf(USAGE);
				exit(EXIT_ERR_ARG);
			}

			// Save port number
			i++;
			args.port = atoi(argv[i]);

			// Check if port is in a valid range
			if (args.port < TCP_PORT_LOWER_LIM ||
					args.port > TCP_PORT_HIGHER_LIM) {
				printf(P_ERROR2, TCP_PORT_LOWER_LIM, TCP_PORT_HIGHER_LIM);
				printf(USAGE);
				exit(EXIT_ERR_ARG);
			}

			printf(P_INFO, args.port);
		} else {
			printf(ARG_ERROR, argv[i]);
			printf(USAGE);
			exit(EXIT_ERR_ARG);
		}
	}

	return args;
}


/**
 * @brief Safely terminate the daemon process
 *
 * @param[in]	errcode	Error code
 */
void safe_exit(int errcode) {
	// TODO: Implement safety features
	//			- Close opened files (?)
	//			- Free memory (?)

	perror("yashd: Exiting daemon...");
	exit(errcode);
}


/**
 * @brief Handler for SIGPIPE
 *
 * If we are waiting reading from a pipe and
 * the interlocutor dies abruptly (say because
 * of ^C or kill -9), then we receive a SIGPIPE
 * signal. Here we handle that.
 *
 * Based on an example provided in Unix Systems Programming by Ramesh
 * Yerraballi.
 *
 * @param	sig	Signal
 */
void sig_pipe(int sig) {
	perror("Broken pipe signal");
}


/**
 * @brief Handler for SIGCHLD signal
 *
 * Based on an example provided in Unix Systems Programming by Ramesh
 * Yerraballi.
 *
 * @param	sig	Signal
 */
void sig_chld(int sig) {
	int status;
	fprintf(stderr, "Child terminated\n");
	wait(&status); // So no zombies
}


/**
 * @brief Initializes the current program as a daemon, by changing working
 *  directory, umask, and eliminating control terminal, setting signal handlers,
 *  saving pid, making sure that only one daemon is running.
 *
 * Modified from Ramesh Yerraballi.
 *
 * @param[in] path is where the daemon eventually operates
 * @param[in] mask is the umask typically set to 0
 */
void daemon_init(const char *const path, uint mask) {
	pid_t pid;
	char buff[256];
	static FILE *log; // for the log
	int fd;
	int k;

	// Put server in background (with init/systemd as parent)
	if ((pid = fork()) < 0) {
		perror("daemon_init: Cannot fork process");
		safe_exit(EXIT_DAEMON);	// TODO: Evaluate if we need this safe exit
	} else if (pid > 0) {	// Parent
		// No need for safe exit because parent is done
		exit(EXIT_OK);
	}

	// Child

	// Close all file descriptors that are open
	for (k = getdtablesize() - 1; k > 0; k--)
		close(k);

	// Redirect stdin and stdout to /dev/null
	if ((fd = open("/dev/null", O_RDWR)) < 0) {
		perror("daemon_init: Error: Failed to open /dev/null");
		safe_exit(EXIT_DAEMON);	// TODO: Evaluate if we need this safe exit
	}
	dup2(fd, STDIN_FILENO); /* detach stdin */
	dup2(fd, STDOUT_FILENO); /* detach stdout */
	close(fd);
	// From this point on printf and scanf have no effect

	// Redirect stderr to u_log_path
	log = fopen(log_path, "aw");	// attach stderr to log file
	fd = fileno(log);	// Obtain file descriptor of the log
	dup2(fd, STDERR_FILENO);
	close(fd);
	// From this point on printing to stderr will go to log file

	// Set signal handlers
	if (signal(SIGCHLD, sig_chld) < 0) {
		perror("daemon_init: Error: Could not set signal handler for SIGCHLD");
		safe_exit(EXIT_DAEMON);	// TODO: Evaluate if we need this safe exit
	}
	if (signal(SIGPIPE, sig_pipe) < 0) {
		perror("daemon_init: Error: Could not set signal handler for SIGPIPE");
		safe_exit(EXIT_DAEMON);	// TODO: Evaluate if we need this safe exit
	}

	/*
	// From old yash code
	signal(SIGTTOU, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	 */

	// Change directory to specified safe directory
	chdir(path);

	// Set umask to mask (usually 0)
	umask(mask);

	// Detach controlling terminal by becoming session leader
	setsid();

	// Put self in a new process group
	pid = getpid();
	setpgrp();	// GPI: modified for linux

	/* Make sure only one server is running */
	if ((k = open(pid_path, O_RDWR | O_CREAT, 0666)) < 0) {
		perror("daemon_init: Error: Could not open PID file");
		safe_exit(EXIT_DAEMON);	// TODO: Evaluate if we need this safe exit
	}
	if (lockf(k, F_TLOCK, 0) != 0) {
		perror("daemon_init: Warning: Could not lock PID file because other "
				"daemon instance is running");
		safe_exit(EXIT_DAEMON);	// TODO: Evaluate if we need this safe exit
	}

	/* Save server's pid without closing file (so lock remains)*/
	sprintf(buff, "%6d", pid);
	write(k, buff, strlen(buff));

	return;
}


/**
 * @brief Check for input that should be ignored.
 *
 * This function checks for empty input strings and strings consisting of
 * whitespace characters only, and ignores such input.
 *
 * @param input_str	Inupt string to check
 * @return	1 if the input should be ignored, 0 otherwise
 */
bool ignoreInput(char* input_str) {
	// Check for empty command string
	if (!strcmp(input_str, EMPTY_STR)) {
		return (true);
	}

	// Check for a command string containing only whitespace
	uint8_t whitespace = true;
	for (size_t i=0; i<strlen(input_str); i++) {
		if (!isspace(input_str[i])) {
			whitespace = false;
		}
	}

	if (whitespace) {
		return (true);
	}

	return (false);
}


/**
 * @brief Remove job from job_arr
 *
 * @param	job_idx	Job index in the job_arr
 */
void removeJob(int job_idx) {
	// Clear job entries
	job_arr[job_idx].jobno = 0;
	job_arr[job_idx].gpid = 0;
	strcpy(job_arr[job_idx].status, "\0");

	// Decrease last job number if necessary
	if (job_idx == last_job) {
		// Find the next job
		bool next_job_found = false;
		while (!next_job_found) {
			last_job--;
			if (job_arr[last_job].jobno > 0) {
				next_job_found = true;
			}
		}
	}
}


/**
 * @brief Print job information
 *
 * @param	job_idx	Job array index
 */
void printJob(int job_idx) {
	// Print the job number
	printf("[%d]", job_arr[job_idx].jobno);

	// Print current job indicator
	if (last_job == job_idx) {
		printf("+");
	} else {
		printf("-");
	}

	// Print job status
	printf(" %s", job_arr[job_idx].status);

	// Print job command string
	printf("\t");
	for (int j=0; j<job_arr[job_idx].cmd_tok_len; j++) {
		printf("%s ", job_arr[job_idx].cmd_tok[j]);
	}
	printf("\n");
}


/**
 * @brief Send command to the background.
 *
 * TODO: Implement bg
 */
void bgExec() {

}


/**
 * @brief Send command to the foreground.
 *
 * TODO: Implement fg
 */
void fgExec() {

}


/**
 * @brief Display jobs table.
 *
 * TODO: Implement jobs
 */
void jobsExec() {
	// Update the jobs table
	maintainJobsTable();

	// Check we at least have one job in the list
	if (last_job <= EMPTY_ARRAY) {
		printf("No jobs in job table\n");
		return;
	}

	// Iterate over all the jobs in the array
	for (int i=0; i<=last_job; i++) {
		// Only print active jobs
		if (!strcmp(job_arr[i].status, JOB_STATUS_RUNNING) ||
				!strcmp(job_arr[i].status, JOB_STATUS_STOPPED)) {
			// Print the job info
			printJob(i);
		}
	}
}


/**
 * @brief Check if input is shell command, and run it.
 *
 * @param	input	Raw input string
 * @return	True if shell command ran, false if it is not a shell command
 */
bool runShellCmd(char* input) {
	if (!strcmp(input, CMD_BG)) {
		bgExec();
		return true;
	} else if (!strcmp(input, CMD_FG)) {
		fgExec();
		return true;
	} else if (!strcmp(input, CMD_JOBS)) {
		jobsExec();
		return true;
	}
	return false;
}


/**
 * @brief Split a command string into string tokens.
 *
 * This function assumes the `cmd.cmd_str` is not an empty string.
 *
 * This function uses strtok() to split a command string into string tokens.
 * Tokens are considered to be contiguous characters separated by whitespace.
 *
 * @param	cmd	Command struct
 *
 * @sa strtok(), Cmd
 */
void tokenizeString(Job* cmd) {
	const char CMD_TOKEN_DELIM = ' ';	// From requirements
	size_t len = 0;

	// Remove final newline char and replace with NULL char
	len = strlen(cmd->cmd_str);
	if (cmd->cmd_str[len-1] == '\n') {
		cmd->cmd_str[len-1] = '\0';
	}

	// Break down the command into tokens using strtok
	cmd->cmd_tok[0] = strtok(cmd->cmd_str, &CMD_TOKEN_DELIM);
	uint32_t count = 1;	// Start at one because we already run strtok once

	while ((cmd->cmd_tok[count] = strtok(NULL, &CMD_TOKEN_DELIM))) {
		count++;
	}
	cmd->cmd_tok_len = count;
}


/**
 * @brief Parse a command.
 *
 * This function assumes the `cmd.cmd_str` is not an empty string.
 *
 * This function takes a raw command string, and parses it to load it into a
 * `Job` struct as per the requirements.
 *
 * TODO: Might need check for multiple redirections of the same type to raise an error.
 *
 * @param	cmd_str		Raw command string
 * @param	job_arr	Jobs list of parsed commands
 * @param	last_job	Last job added to the job_arr
 *
 * @sa	Cmd
 */
void parseJob(char* cmd_str, Job job_arr[], int* last_job) {
	const char I_REDIR_OPT[2] = "<\0";
	const char O_REDIR_OPT[2] = ">\0";
	const char E_REDIR_OPT[3] = "2>\0";
	const char BG_OPT[2] = "&\0";
	const char PIPE_OPT[2] = "|\0";
	const char SYNTAX_ERR_1[MAX_ERROR_LEN] = "syntax error: command should not"
			" start with \0";
	const char SYNTAX_ERR_2[MAX_ERROR_LEN] = "syntax error: near token \0";
	const char SYNTAX_ERR_3[MAX_ERROR_LEN] = "syntax error: command should not"
			" end with \0";
	const char SYNTAX_ERR_4[MAX_ERROR_LEN] = "syntax error: & should be the last"
			" token of the command\0";

	// Save and tokenize command string
	strcpy(job_arr[*last_job].cmd_str, cmd_str);
	tokenizeString(&job_arr[*last_job]);

	/*
	 * Iterate over all tokens to look for arguments, redirection directives,
	 * pipes and background directives
	 */
	int cmd_count = 0;	// Command array counter
	for (uint32_t i=0; i<job_arr[*last_job].cmd_tok_len; i++) {
		if (!strcmp(I_REDIR_OPT, job_arr[*last_job].cmd_tok[i])) {	// Check for input redir
			// Check if redirection token has the correct syntax
			if (i <= 0 || cmd_count <= 0) {	// Check it is not the first token
				strcpy(job_arr[*last_job].err_msg, SYNTAX_ERR_1);
				strcat(job_arr[*last_job].err_msg, job_arr[*last_job].cmd_tok[i]);
				return;
			} else if (i >= job_arr[*last_job].cmd_tok_len-1) {	// Check it is not the last token
				strcpy(job_arr[*last_job].err_msg, SYNTAX_ERR_3);
				strcat(job_arr[*last_job].err_msg, job_arr[*last_job].cmd_tok[i]);
				return;
			} else if (!strcmp(I_REDIR_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(O_REDIR_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(E_REDIR_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(PIPE_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(BG_OPT, job_arr[*last_job].cmd_tok[i+1])) {	// Check there is an argument after this token
				strcpy(job_arr[*last_job].err_msg, SYNTAX_ERR_2);
				strcat(job_arr[*last_job].err_msg, job_arr[*last_job].cmd_tok[i]);
				return;
			} else {	// Correct syntax
				i++;	// Move ahead one iter to get the redir argument
				if (!job_arr[*last_job].pipe) {
					strcpy(job_arr[*last_job].in1, job_arr[*last_job].cmd_tok[i]);
				} else {
					strcpy(job_arr[*last_job].in2, job_arr[*last_job].cmd_tok[i]);
				}
			}
		} else if (!strcmp(O_REDIR_OPT,job_arr[*last_job].cmd_tok[i])) {	// Output redir
			// Check if redirection token has the correct syntax
			if (i <= 0 || cmd_count <= 0) {	// Check it is not the first token
				strcpy(job_arr[*last_job].err_msg, SYNTAX_ERR_1);
				strcat(job_arr[*last_job].err_msg, job_arr[*last_job].cmd_tok[i]);
				return;
			} else if (i >= job_arr[*last_job].cmd_tok_len-1) {	// Check it is not the last token
				strcpy(job_arr[*last_job].err_msg, SYNTAX_ERR_3);
				strcat(job_arr[*last_job].err_msg, job_arr[*last_job].cmd_tok[i]);
				return;
			} else if (!strcmp(I_REDIR_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(O_REDIR_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(E_REDIR_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(PIPE_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(BG_OPT, job_arr[*last_job].cmd_tok[i+1])) {	// Check there is an argument after this token
				strcpy(job_arr[*last_job].err_msg, SYNTAX_ERR_2);
				strcat(job_arr[*last_job].err_msg, job_arr[*last_job].cmd_tok[i]);
				return;
			} else {	// Correct syntax
				i++;	// Move ahead one iter to get the redir argument
				if (!job_arr[*last_job].pipe) {
					strcpy(job_arr[*last_job].out1, job_arr[*last_job].cmd_tok[i]);
				} else {
					strcpy(job_arr[*last_job].out2, job_arr[*last_job].cmd_tok[i]);
				}
			}
		} else if (!strcmp(E_REDIR_OPT, job_arr[*last_job].cmd_tok[i])) {	// Error redir
			// Check if redirection token has the correct syntax
			if (i <= 0 || cmd_count <= 0) {	// Check it is not the first token
				strcpy(job_arr[*last_job].err_msg, SYNTAX_ERR_1);
				strcat(job_arr[*last_job].err_msg, job_arr[*last_job].cmd_tok[i]);
				return;
			} else if (i >= job_arr[*last_job].cmd_tok_len-1) {	// Check it is not the last token
				strcpy(job_arr[*last_job].err_msg, SYNTAX_ERR_3);
				strcat(job_arr[*last_job].err_msg, job_arr[*last_job].cmd_tok[i]);
				return;
			} else if (!strcmp(I_REDIR_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(O_REDIR_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(E_REDIR_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(PIPE_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(BG_OPT, job_arr[*last_job].cmd_tok[i+1])) {	// Check there is an argument after this token
				strcpy(job_arr[*last_job].err_msg, SYNTAX_ERR_2);
				strcat(job_arr[*last_job].err_msg, job_arr[*last_job].cmd_tok[i]);
				return;
			} else {	// Correct syntax
				i++;	// Move ahead one iter to get the redir argument
				if (!job_arr[*last_job].pipe) {
					strcpy(job_arr[*last_job].err1, job_arr[*last_job].cmd_tok[i]);
				} else {
					strcpy(job_arr[*last_job].err2, job_arr[*last_job].cmd_tok[i]);
				}
			}
		} else if (!strcmp(PIPE_OPT, job_arr[*last_job].cmd_tok[i])) {	// Pipe command
			// Check if pipe token has the correct syntax
			if (i <= 0 || cmd_count <= 0) {	// Check it is not the first token
				strcpy(job_arr[*last_job].err_msg, SYNTAX_ERR_1);
				strcat(job_arr[*last_job].err_msg, job_arr[*last_job].cmd_tok[i]);
				return;
			} else if (i >= job_arr[*last_job].cmd_tok_len-1) {	// Check it is not the last token
				strcpy(job_arr[*last_job].err_msg, SYNTAX_ERR_3);
				strcat(job_arr[*last_job].err_msg, job_arr[*last_job].cmd_tok[i]);
				return;
			} else if (!strcmp(I_REDIR_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(O_REDIR_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(E_REDIR_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(PIPE_OPT, job_arr[*last_job].cmd_tok[i+1]) ||
					!strcmp(BG_OPT, job_arr[*last_job].cmd_tok[i+1])) {	// Check there is an argument after this token
				strcpy(job_arr[*last_job].err_msg, SYNTAX_ERR_2);
				strcat(job_arr[*last_job].err_msg, job_arr[*last_job].cmd_tok[i]);
				return;
			} else {	// Correct syntax
				job_arr[*last_job].pipe = true;
				cmd_count = 0;	// Start argument count for cmd2
			}
		} else if (!strcmp(BG_OPT, job_arr[*last_job].cmd_tok[i])) {	// Background command
			// Check if background token has the correct syntax
			if (i != job_arr[*last_job].cmd_tok_len-1) {	// Check if it is not the last token
				strcpy(job_arr[*last_job].err_msg, SYNTAX_ERR_4);
				return;
			} else {
				job_arr[*last_job].bg = true;
			}
		} else {	// Command argument
			if (!job_arr[*last_job].pipe) {
				job_arr[*last_job].cmd1[cmd_count] = job_arr[*last_job].cmd_tok[i];
				cmd_count++;
			} else {
				job_arr[*last_job].cmd2[cmd_count] = job_arr[*last_job].cmd_tok[i];
				cmd_count++;
			}
		}
	}
}


/**
 * @brief Redirect input, output and error of simple commands without pipes, or
 * left child of a pipe.
 *
 * @param	cmd	Command to set the redirection
 */
void redirectSimple(Job* cmd) {
	const char REDIR_ERR_1[MAX_ERROR_LEN] = "open errno ";
	const char REDIR_ERR_2[MAX_ERROR_LEN] = ": could not open file: ";
	extern errno;
	char errno_str[sizeof(int)*8+1];

	// Input redirection
	if (strcmp(cmd->in1, EMPTY_STR)) {
		//
		int i1fd = open(cmd->in1, O_RDONLY,
						S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

		if (i1fd == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, REDIR_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, REDIR_ERR_2);
			strcat(cmd->err_msg, cmd->in1);
			return;
		}

		dup2(i1fd, STDIN_FILENO);
		close(i1fd);
	}

	// Output redirection
	if (strcmp(cmd->out1, EMPTY_STR)) {
		// Create output file
		int o1fd = open(cmd->out1, O_WRONLY|O_CREAT|O_TRUNC,
				S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

		if (o1fd == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, REDIR_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, REDIR_ERR_2);
			strcat(cmd->err_msg, cmd->out1);
			return;
		}

		dup2(o1fd, STDOUT_FILENO);
		close(o1fd);
	}

	// Error redirection
	if (strcmp(cmd->err1, EMPTY_STR)) {
		//
		int e1fd = open(cmd->err1, O_WRONLY|O_CREAT|O_TRUNC,
						S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

		if (e1fd == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, REDIR_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, REDIR_ERR_2);
			strcat(cmd->err_msg, cmd->err1);
			return;
		}

		dup2(e1fd, STDERR_FILENO);
		close(e1fd);
	}
}


/**
 * @brief Redirect input, output and error of right child of a pipe.
 *
 * @param	cmd	Command to set the redirection
 */
void redirectPipe(Job* cmd) {
	const char REDIR_ERR_1[MAX_ERROR_LEN] = "open errno ";
	const char REDIR_ERR_2[MAX_ERROR_LEN] = ": could not open file: ";
	extern errno;
	char errno_str[sizeof(int)*8+1];

	// Input redirection
	if (cmd->pipe && strcmp(cmd->in2, EMPTY_STR)) {
		//
		int i2fd = open(cmd->in2, O_RDONLY,
						S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

		if (i2fd == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, REDIR_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, REDIR_ERR_2);
			strcat(cmd->err_msg, cmd->in2);
			return;
		}

		dup2(i2fd, STDIN_FILENO);
		close(i2fd);
	}

	// Output redirection
	if (cmd->pipe && strcmp(cmd->out2, EMPTY_STR)) {
		//
		int o2fd = open(cmd->out2, O_WRONLY|O_CREAT|O_TRUNC,
						S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

		if (o2fd == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, REDIR_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, REDIR_ERR_2);
			strcat(cmd->err_msg, cmd->out2);
			return;
		}

		dup2(o2fd, STDOUT_FILENO);
		close(o2fd);
	}

	// Error redirection
	if (cmd->pipe && strcmp(cmd->err2, EMPTY_STR)) {
		//
		int e2fd = open(cmd->err2, O_WRONLY|O_CREAT|O_TRUNC,
						S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

		if (e2fd == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, REDIR_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, REDIR_ERR_2);
			strcat(cmd->err_msg, cmd->err2);
			return;
		}

		dup2(e2fd, STDERR_FILENO);
		close(e2fd);
	}

}


/**
 * @brief Set up signal handling to relay signals to children processes.
 *
 * This function is based on the UT Austin EE 382V Systems Programming class
 * examples posted by Dr. Ramesh Yerraballi.
 *
 * TODO: Add support for job control.
 *
 * @param cmd	Parsed command
 */
void waitForChildren(Job* cmd) {
	const char SIG_ERR_1[MAX_ERROR_LEN] = "signal errno ";
	const char SIG_ERR_2[MAX_ERROR_LEN] = ": waitpid error";
	extern errno;
	char errno_str[sizeof(int)*8+1];

	int status;
	uint8_t count = 0;
	int child_num;

	// Determine the number of child processes
	if (cmd->pipe) {
		child_num = CHILD_COUNT_PIPE;
	} else {
		child_num = CHILD_COUNT_SIMPLE;
	}

	// Wait for child to exit
	while (count < child_num) {
		/**
		 * TODO: Fix EINTR (4) error
		 *
		 * This snippet always throws the EINTR (4) error code. The
		 * documentation for waitpid (3) says EINTR means: "WNOHANG was not set
		 * and an unblocked signal or a SIGCHLD was caught; see signal(7)."
		 *
		 * TODO: Fix WCONTINUED compilation error
		 *
		 * See this for error description: https://stackoverflow.com/questions/
		 * 60101242/compiler-error-using-wcontinued-option-for-waitpid
		 */
		//if (waitpid(-1, &status, WUNTRACED|WCONTINUED) == SYSCALL_RETURN_ERR) {
		if (waitpid(job_arr[last_job].gpid, &status, WUNTRACED) == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, SIG_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, SIG_ERR_2);
			return;
		}

		if (WIFEXITED(status)) {
			if (verbose) {
				printf("-yash: child process terminated normally\n");
			}
			count++;
		} else if (WIFSIGNALED(status)) {
			printf("\n");	// Ensure there is an space after "^C"
			if (verbose) {
				printf("-yash: child process terminated by a signal\n");
			}
			count++;
		} else if (WIFSTOPPED(status)) {
			printf("\n");	// Ensure there is an space after "^Z"
			if (verbose) {
				printf("-yash: child process stopped by a signal\n");
			}
			//
		} /*else if (WIFCONTINUED(status)) {
			//
		}*/
	}
}


/**
 * @brief Execute commands.
 *
 * This function allows for both simple and piped commands (1 pipe only).
 * Moreover, the command is checked for correctness, and ignored if it does not
 * exist.
 *
 * This function is based on the UT Austin EE 382V Systems Programming class
 * examples by Dr. Ramesh Yerraballi.
 *
 * TODO: Add support for job control.
 *
 * @param	job_arr	Jobs list of parsed commands
 * @param	last_job	Last job added to the job_arr
 */
void runJob(Job job_arr[], int* last_job) {
	const char PIPE_ERR_1[MAX_ERROR_LEN] = "pipe errno ";
	const char PIPE_ERR_2[MAX_ERROR_LEN] = ": failed to make pipe";
	extern errno;
	char errno_str[sizeof(int)*8+1];

	pid_t c1_pid, c2_pid;
	int pfd[2];
	int stdout_fd;

	if (job_arr[*last_job].pipe) {
		stdout_fd = dup(STDOUT_FILENO);	// Save stdout

		if (pipe(pfd) == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(job_arr[*last_job].err_msg, PIPE_ERR_1);
			strcat(job_arr[*last_job].err_msg, errno_str);
			strcat(job_arr[*last_job].err_msg, PIPE_ERR_2);
			return;
		}
	}

	c1_pid = fork();

	if (c1_pid == 0) {	// Child 1 or left child process
		// Create a new session and a new group, and become group leader
		setpgid(0, 0);

		// Set up signal handling sent to the children process group
		if (verbose) {
			printf("-yash: children process group: ignoring signal SIGTTOU, "
					"but getting all the others\n");
		}
		signal(SIGTTOU, SIG_IGN);
		signal(SIGINT, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
		signal(SIGCHLD, SIG_DFL);

		if (job_arr[*last_job].pipe) {
			close(pfd[0]);	// Close unused read end
			dup2(pfd[1], STDOUT_FILENO);	// Make output go to pipe
		}
		// Do additional redirection if necessary
		redirectSimple(&job_arr[*last_job]);
		if (strcmp(job_arr[*last_job].err_msg, EMPTY_STR)) {
			if (job_arr[*last_job].pipe) {
				dup2(stdout_fd, STDOUT_FILENO);	// Allow to write to stdout
			}
			printf("-yash: %s\n", job_arr[*last_job].err_msg);
			exit(EXIT_ERR_CMD);
		}

		// Execute command
		if (execvp(job_arr[*last_job].cmd1[0], job_arr[*last_job].cmd1) == SYSCALL_RETURN_ERR
				&& verbose) {
			printf("-yash: execvp() errno: %d\n", errno);
		}
		// Make sure we terminate child on execvp() error
		exit(EXIT_ERR_CMD);
	} else {	// Parent process
		if (job_arr[*last_job].pipe) {
			c2_pid = fork();

			if (c2_pid == 0) {	// Child 2 or right child process
				// Join the group created by child 1
				setpgid(0, c1_pid);

				// Set up signal handling sent to the children process group
				if (verbose) {
					printf("-yash: children process group: ignoring signal SIGTTOU, "
							"but getting all the others\n");
				}
				signal(SIGTTOU, SIG_IGN);
				signal(SIGINT, SIG_DFL);
				signal(SIGTSTP, SIG_DFL);

				close(pfd[1]);	// Close unused write end
				dup2(pfd[0], STDIN_FILENO);	// Get input from pipe

				// Do additional redirection if necessary
				redirectPipe(&job_arr[*last_job]);
				if (strcmp(job_arr[*last_job].err_msg, EMPTY_STR)) {
					dup2(stdout_fd, STDOUT_FILENO);	// Allow to write to stdout
					printf("-yash: %s\n", job_arr[*last_job].err_msg);
					exit(EXIT_ERR_CMD);
				}

				// Execute command
				if (execvp(job_arr[*last_job].cmd2[0], job_arr[*last_job].cmd2) == SYSCALL_RETURN_ERR
						&& verbose) {
					printf("-yash: execvp() errno: %d\n", errno);
				}
				// Make sure we terminate child on execvp() error
				exit(EXIT_ERR_CMD);
			}
			// Parent process. Close pipes so EOF can work
			close(pfd[0]);
			close(pfd[1]);
			close(stdout_fd);
		}

		// Parent process
		// Save job gpid
		job_arr[*last_job].gpid = c1_pid;
		if (!job_arr[*last_job].bg) {
			// Give terminal control to child
			if (verbose) {
				printf("-yash: "
						"giving terminal control to child process group\n");
			}
			tcsetpgrp(0, c1_pid);

			// Block while waiting for children
			waitForChildren(&job_arr[*last_job]);
			if (strcmp(job_arr[*last_job].err_msg, EMPTY_STR)) {
				return;
			}

			// Get back terminal control to parent
			if (verbose) {
				printf("-yash: returning terminal control to parent process\n");
			}
			tcsetpgrp(0, getpid());
			removeJob(*last_job);	// Remove job from jobs table
		}
	}
}


/**
 * @brief Handle new job.
 *
 * This function parses the raw input of the new job, adds the job to the jobs
 * table, and it executes the new job.
 *
 * @param	input	Raw input of the new job
 */
void handleNewJob(char* input) {
	// Initialize a new Job struct
	Job job = {
		EMPTY_STR,		// cmd_str
		{ EMPTY_STR },	// cmd_tok
		0,				// cmd_tok_size
		{ EMPTY_STR },	// cmd1
		EMPTY_STR,		// in1
		EMPTY_STR,		// out1
		EMPTY_STR,		// err1
		{ EMPTY_STR },	// cmd2
		EMPTY_STR,		// in2
		EMPTY_STR,		// out2
		EMPTY_STR,		// err2
		false,			// pipe
		false,			// bg
		EMPTY_ARRAY,	// gpid
		EMPTY_ARRAY,	// jobno
		EMPTY_STR,		// status
		EMPTY_STR		// err_msg
	};

	// Add command to the jobs array
	if (last_job < MAX_CONCURRENT_JOBS) {
		last_job++;
		job_arr[last_job] = job;
		job_arr[last_job].jobno = last_job + 1;
		strcpy(job_arr[last_job].status, JOB_STATUS_RUNNING);
	} else {
		printf("-yash: max number of concurrent jobs reached: %d",
				last_job);
		return;
	}

	// Parse job
	if (verbose) {
		printf("-yash: parsing input...\n");
	}
	parseJob(input, job_arr, &last_job);
	if (strcmp(job_arr[last_job].err_msg, EMPTY_STR)) {
		printf("-yash: %s\n", job_arr[last_job].err_msg);
		return;
	}

	// Run job
	if (verbose) {
		printf("-yash: executing command...\n");
	}
	runJob(job_arr, &last_job);
	if (last_job != EMPTY_ARRAY) {
		if (strcmp(job_arr[last_job].err_msg, EMPTY_STR)) {
			printf("-yash: %s\n", job_arr[last_job].err_msg);
			return;
		}
	}
}

/**
 * @brief Check if any background jobs finished.
 *
 * Check if any previously running job in the jobs table has finished running.
 *
 * TODO: Implement
 */
void maintainJobsTable() {
	// Check every job in the job_arr
	for (int i=0; i<=last_job; i++) {
		// Skip jobs that already finished
		if (!strcmp(job_arr[i].status, JOB_STATUS_RUNNING) ||
				!strcmp(job_arr[i].status, JOB_STATUS_STOPPED)) {
			int status;
			if (waitpid(job_arr[i].gpid, &status, WNOHANG|WUNTRACED|WCONTINUED) == SYSCALL_RETURN_ERR) {
				printf("-yash: error checking child %d status: %d\n",
						job_arr[i].gpid, errno);
				// TODO: handle error
			}
			if (WIFEXITED(status)) {
				if (verbose) {
					printf("-yash: child process terminated normally\n");
				}

				// Change status to done and, remove child from array
				strcpy(job_arr[i].status, JOB_STATUS_DONE);
				printJob(i);
				removeJob(i);

			} else if (WIFSIGNALED(status)) {
				if (verbose) {
					printf("-yash: child process terminated by a signal\n");
				}

				// Change status to done, and remove child from array
				strcpy(job_arr[i].status, JOB_STATUS_DONE);
				printJob(i);
				removeJob(i);
			} else if (WIFSTOPPED(status)) {
				if (verbose) {
					printf("-yash: child process stopped by a signal\n");
				}

				// Change status to stopped
				strcpy(job_arr[i].status, JOB_STATUS_STOPPED);
			} else if (WIFCONTINUED(status)) {
				if (verbose) {
					printf("-yash: child process continued by a signal\n");
				}

				// Change status to running
				strcpy(job_arr[i].status, JOB_STATUS_RUNNING);
			}
		}
	}
}

/**
 * @brief Send a SIGKILL to all jobs in the jobs list
 */
void killAllJobs() {
	for (int i=0; i<last_job; i++) {
			// Skip jobs that already finished
			if (!strcmp(job_arr[i].status, JOB_STATUS_RUNNING) ||
					!strcmp(job_arr[i].status, JOB_STATUS_STOPPED)) {
				kill(job_arr[i].gpid, SIGKILL);
			}
	}
}


/**
 * @brief Point of entry.
 *
 * @param argc	Number of command line arguments
 * @param argv	Array of command line arguments
 * @return	Errorcode
 */
int main(int argc, char** argv) {
	/*
	char* in_str;
	 */
	// Process command line arguments
	args = parseArgs(argc, argv);

	// Initialize the daemon
	strcpy(log_path, DAEMON_LOG_PATH);
	strcpy(pid_path, DAEMON_PID_PATH);
	daemon_init(DAEMON_DIR, DAEMON_UMASK);

	// Old yash code

	/*
	 * Use `readline()` to control when to exit from the shell. Typing
	 * [Ctrl]+[D] on an empty prompt line will exit as stated in the
	 * requirements. From `readline()` documentation:
	 *
	 * "If readline encounters an EOF while reading the line, and the line is
	 * empty at that point, then (char *)NULL is returned. Otherwise, the line
	 * is ended just as if a newline had been typed."
	 */
	/*
	while ((in_str = readline("# "))) {
		// Check input to ignore and show the prompt again
		if (verbose) {
			printf("-yash: checking if input should be ignored...\n");
		}

		// Check if input should be ignored
		if (ignoreInput(in_str)) {
			if (verbose) {
				printf("-yash: input ignored\n");
			}
		} else if (runShellCmd(in_str)) {	// Check if input is a shell command
			if (verbose) {
				printf("-yash: ran shell command\n");
			}
		} else {	// Handle new job
			if (verbose) {
				printf("-yash: new job\n");
			}
			handleNewJob(in_str);
		}

		// Check for finished jobs
		maintainJobsTable();
	}

	// Ensure a new-line on exit
	printf("\n");
	if (verbose) {
		printf("-yash: exiting...\n");
	}
	killAllJobs();

	// TODO: Ensure all child processes are dead on exit
	*/

	// For now do nothing
	while(true) {
		// Sleep
		sleep(10);
		perror("Run yashd main loop");
	}

	return (EXIT_OK);
}
