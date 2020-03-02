/**
 * @file yashd.c
 *
 * @brief YASH shell.
 *
 * @author:	Jose Carlos Martinez Garcia-Vaso <carlosgvaso@gmail.com>
 */

#include "yashd.h"


// Globals
static cmd_args_t args;

extern int errno;

static char log_path[PATHMAX+1];
static char pid_path[PATHMAX+1];


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
cmd_args_t parseArgs(int argc, char** argv) {
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
		cmd_args_t args = {false, DAEMON_PORT};

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
void safeExit(int errcode) {
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
void sigPipe(int sig) {
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
void sigChld(int sig) {
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
void daemonInit(const char *const path, uint mask) {
	pid_t pid;
	char buff[256];
	static FILE *log; // for the log
	int fd;
	int k;

	// Put server in background (with init/systemd as parent)
	if ((pid = fork()) < 0) {
		perror("daemon_init: Cannot fork process");
		safeExit(EXIT_DAEMON);	// TODO: Evaluate if we need this safe exit
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
		safeExit(EXIT_DAEMON);	// TODO: Evaluate if we need this safe exit
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
	if (signal(SIGCHLD, sigChld) < 0) {
		perror("daemon_init: Error: Could not set signal handler for SIGCHLD");
		safeExit(EXIT_DAEMON);	// TODO: Evaluate if we need this safe exit
	}
	if (signal(SIGPIPE, sigPipe) < 0) {
		perror("daemon_init: Error: Could not set signal handler for SIGPIPE");
		safeExit(EXIT_DAEMON);	// TODO: Evaluate if we need this safe exit
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
		safeExit(EXIT_DAEMON);	// TODO: Evaluate if we need this safe exit
	}
	if (lockf(k, F_TLOCK, 0) != 0) {
		perror("daemon_init: Warning: Could not lock PID file because other "
				"daemon instance is running");
		safeExit(EXIT_DAEMON);	// TODO: Evaluate if we need this safe exit
	}

	/* Save server's pid without closing file (so lock remains)*/
	sprintf(buff, "%6d", pid);
	write(k, buff, strlen(buff));

	return;
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
	daemonInit(DAEMON_DIR, DAEMON_UMASK);

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
