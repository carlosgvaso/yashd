/**
 * @file yashd.c
 *
 * @brief Yash shell daemon.
 *
 * @author:	Jose Carlos Martinez Garcia-Vaso <carlosgvaso@gmail.com>
 */

#include "yashd.h"


// Globals
static cmd_args_t args;

extern int errno;

static char log_path[PATHMAX+1];
static char pid_path[PATHMAX+1];

th_info_t th_info_table[MAX_CONCURRENT_CLIENTS];
int th_table_idx = 0;


/**
 * @brief Generate a string with the current timestamp in syslog format
 *
 * @param	buff	Buffer to hold the timestamp
 * @param	size	Buffer size
 * @return	String with current timestamp in syslog format
 */
char *timeStr(char *buff, int size) {
	struct tm sTm;

	time_t now = time(NULL);
	gmtime_r(&now, &sTm);

	if (!strftime(buff, size, "%b %e %H:%M:%S", &sTm)) {
		perror("Could not format timestamp");
		strcpy(buff, "Jan  1 00:00:00\0");
	}

	return buff;
}


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
		safeExit(EXIT_ERR_DAEMON);	// TODO: Evaluate if we need this safe exit
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
		safeExit(EXIT_ERR_DAEMON);	// TODO: Evaluate if we need this safe exit
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
		safeExit(EXIT_ERR_DAEMON);	// TODO: Evaluate if we need this safe exit
	}
	if (signal(SIGPIPE, sigPipe) < 0) {
		perror("daemon_init: Error: Could not set signal handler for SIGPIPE");
		safeExit(EXIT_ERR_DAEMON);	// TODO: Evaluate if we need this safe exit
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
		safeExit(EXIT_ERR_DAEMON);	// TODO: Evaluate if we need this safe exit
	}
	if (lockf(k, F_TLOCK, 0) != 0) {
		perror("daemon_init: Warning: Could not lock PID file because other "
				"daemon instance is running");
		safeExit(EXIT_ERR_DAEMON);	// TODO: Evaluate if we need this safe exit
	}

	/* Save server's pid without closing file (so lock remains)*/
	sprintf(buff, "%6d", pid);
	write(k, buff, strlen(buff));

	return;
}


/**
 * @brief Reuse TCP port
 *
 * @param	s	Socket
 */
void reusePort(int s) {
	int one = 1;

	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*) &one, sizeof(one))
			== -1) {
		char time_buff[BUFF_SIZE_TIMESTAMP];
		fprintf(stderr, "%s yashd[daemon]: ERROR: error in setsockopt, "
				"SO_REUSEPORT \n", timeStr(time_buff, BUFF_SIZE_TIMESTAMP));
		exit(EXIT_ERR_SOCKET);
	}
}


/**
 * @brief Create and open server socket
 *
 * @param	port	Socket port number
 */
int createSocket(int port) {
	char time_buff[BUFF_SIZE_TIMESTAMP];
	char hostname_str[MAX_HOSTNAME_LEN];
	int sd, pn;
	socklen_t length;
	struct sockaddr_in server;
	struct hostent *hp,* gethostbyname();

	// Get host information, NAME and INET ADDRESS
	gethostname(hostname_str, MAX_HOSTNAME_LEN);
	// strcpy(ThisHost,"localhost");

	fprintf(stderr, "%s yashd[daemon]: TCP server running at hostname: %s\n",
			timeStr(time_buff, BUFF_SIZE_TIMESTAMP), hostname_str);

	if ((hp = gethostbyname(hostname_str)) == NULL) {
		fprintf(stderr, "%s yashd[daemon]: ERROR: Cannot find host %s\n",
				timeStr(time_buff, BUFF_SIZE_TIMESTAMP), hostname_str);
		exit(EXIT_ERR_SOCKET);
	}

	bcopy(hp->h_addr, &(server.sin_addr), hp->h_length);
	fprintf(stderr, "%s yashd[daemon]: TCP server INET ADDRESS is: %s\n",
			timeStr(time_buff, BUFF_SIZE_TIMESTAMP),
			inet_ntoa(server.sin_addr));

	// Construct name of socket
	server.sin_family = AF_INET;
	// server.sin_family = hp->h_addrtype;

	server.sin_addr.s_addr = htonl(INADDR_ANY);

	pn = htons(port);
	server.sin_port = pn;

	// Create socket on which to send  and receive
	sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	// sd = socket (hp->h_addrtype,SOCK_STREAM,0);

	if (sd < 0) {
		perror("ERROR: Opening stream socket");
		exit(EXIT_ERR_SOCKET);
	}

	/* Allow the server to re-start quickly instead of waiting for TIME_WAIT
	 * which can be as large as 2 minutes
	 */
	reusePort(sd);
	if (bind(sd, (struct sockaddr*) &server, sizeof(server)) < 0) {
		close(sd);
		perror("ERROR: Binding name to stream socket");
		exit(EXIT_ERR_SOCKET);
	}

	// Get port information and print it out
	length = sizeof(server);
	if (getsockname(sd, (struct sockaddr*) &server, &length)) {
		perror("ERROR: Getting socket name");
		exit(EXIT_ERR_SOCKET);
	}
	fprintf(stderr, "%s yashd[daemon]: INFO: Server Port is: %d\n",
			timeStr(time_buff, BUFF_SIZE_TIMESTAMP), ntohs(server.sin_port));

	// Accept TCP connections from clients
	listen(sd, MAX_CONNECT_QUEUE);

	return sd;
}


/**
 * @brief Thread function to serve the clients
 *
 * @param	thread_args	Arguments passed to the thread as a th_args_t struct
 */
void *serverThread(void *thread_args) {
	th_args_t *th_args = (th_args_t*) thread_args;
	int ps = th_args->ps;
	struct sockaddr_in from = th_args->from;
	char buf[512];
	int rc;
	struct hostent *hp, *gethostbyname();

	if (th_args->cmd_args.verbose) {
		char time_buff[24];
		fprintf(stderr, "%s yashd[thread]: INFO: Starting new thread\n",
				timeStr(time_buff, BUFF_SIZE_TIMESTAMP));
	}

	fprintf(stderr, "Serving %s:%d\n", inet_ntoa(from.sin_addr), ntohs(from.sin_port));
	if ((hp = gethostbyaddr((char*) &from.sin_addr.s_addr,
			sizeof(from.sin_addr.s_addr), AF_INET)) == NULL)
		fprintf(stderr, "Can't find host %s\n", inet_ntoa(from.sin_addr));
	else
		fprintf(stderr, "(Name is : %s)\n", hp->h_name);

	/**  get data from  client and send it back */
	for (;;) {
		fprintf(stderr, "\n...server is waiting...\n");
		if ((rc = recv(ps, buf, sizeof(buf), 0)) < 0) {
			perror("receiving stream  message");
			exit(EXIT_ERR_SOCKET);
		}
		if (rc > 0) {
			buf[rc] = '\0';
			fprintf(stderr, "Received: %s\n", buf);
			fprintf(stderr, "From TCP/Client: %s:%d\n", inet_ntoa(from.sin_addr),
					ntohs(from.sin_port));
			fprintf(stderr, "(Name is : %s)\n", hp->h_name);
			if (send(ps, buf, rc, 0) < 0)
				perror("sending stream message");
		} else {
			fprintf(stderr, "TCP/Client: %s:%d\n", inet_ntoa(from.sin_addr),
					ntohs(from.sin_port));
			fprintf(stderr, "(Name is : %s)\n", hp->h_name);
			fprintf(stderr, "Disconnected..\n");
			close(ps);
			exit(EXIT_OK);
		}
	}
}
/*
	th_args_t *th_args = (th_args_t*) thread_args;
	th_info_table[th_table_idx].my_tid = pthread_self();
	int ps = th_args->ps;
	struct sockaddr_in from = th_args->from;
	char buf[512];
	ssize_t rc;
	struct hostent *hp,* gethostbyname();
	bool run_th = true;
	int new_pid;
	char **args;
	char *buf_copy;

	char *prompt;
	prompt = strdup("# ");
	rc = strlen(prompt);
	if (send(ps, prompt, (size_t) rc, 0) < 0)
		perror("sending stream message");

	if ((hp = gethostbyaddr((char*) &from.sin_addr.s_addr,
			sizeof(from.sin_addr.s_addr), AF_INET)) == NULL)
		fprintf(stderr, "Can't find host %s\n", inet_ntoa(from.sin_addr));
*/
/*
	if (pipe(th_info_table[th_table_idx].pthread_pipe_fd) == -1) {
		perror("pthread pipe\n");
		close(ps);
		exit(EXIT_ERR);
	}

	//  get data from  clients and send it back
	new_pid = fork();
	if (new_pid == 0) {
		close(proc_info_table[table_index_counter].pthread_pipe_fd[1]);
		dup2(proc_info_table[table_index_counter].pthread_pipe_fd[0],
				STDIN_FILENO);
		proc_info_table[table_index_counter].shell_pid = getpid();
		fprintf(stderr, "set table shell pid to %d\n",
				proc_info_table[table_index_counter].shell_pid);

		yash_prog_loop(buf, psd);
	} else if (new_pid > 0) {
		int returned_index = get_proc_info_index_pid(getpid());
		proc_info_table[table_index_counter].shell_pid = new_pid;
		//        printf("my_socket: %d, returned index: %d, pid: %d\n",proc_info_table[returned_index].my_socket, returned_index, getpid());
		close(proc_info_table[table_index_counter].pthread_pipe_fd[0]);

		table_index_counter++;
		for (;;) {
			cleanup(buf);
			if ((rc = recv(psd, buf, sizeof(buf), 0)) < 0) {
				perror("receiving stream  message");
				exit(-1);
			}
			dup2(
					proc_info_table[get_proc_info_index_by_tid(pthread_self())].pthread_pipe_fd[1],
					STDOUT_FILENO);
			if (rc > 0) {
				buf[rc] = '\0';
				buf_copy = strdup(buf);
				args = parseLine(buf_copy);
				if (strcmp(args[0], "CTL") == 0) {
					pid_ch1 = proc_info_table[get_proc_info_index_by_tid(
							pthread_self())].shell_pid;
					if (strcmp(args[1], "c") == 0) {
						fprintf(stderr, "sending sig int to pid %d\n", pid_ch1);
						kill(pid_ch1, SIGINT);
					}
					if (strcmp(args[1], "z") == 0) {
						fprintf(stderr, "sending sig tstp to pid %d\n",
								pid_ch1);
						kill(pid_ch1, SIGTSTP);
					}
				}
				if (strcmp(args[0], "CMD") == 0) {
					write_to_log(buf, (size_t) rc, inet_ntoa(from.sin_addr),
							ntohs(from.sin_port));
					printf("%s", buf);
					//fprintf(stderr, "%s",buf);
					fflush(stdout);
				}
			}
		}
	}
*/
/*
}
*/


/**
 * @brief Point of entry.
 *
 * @param argc	Number of command line arguments
 * @param argv	Array of command line arguments
 * @return	Errorcode
 */
int main(int argc, char **argv) {
	bool run = true;
	char time_buff[BUFF_SIZE_TIMESTAMP];
	int s, ps;
	socklen_t fromlen;
	struct sockaddr_in from;

	// Process command line arguments
	args = parseArgs(argc, argv);

	// Initialize the daemon
	strcpy(log_path, DAEMON_LOG_PATH);
	strcpy(pid_path, DAEMON_PID_PATH);
	daemonInit(DAEMON_DIR, DAEMON_UMASK);

	// Set up server socket
	s = createSocket(args.port);

	// TODO: Accept connections from clients and serve them on a new thread
	fromlen = sizeof(from);
	while(run) {
		if (args.verbose) {
			fprintf(stderr, "%s yashd[daemon]: INFO: Started iteration in "
					"main loop\n",
					timeStr(time_buff, BUFF_SIZE_TIMESTAMP));
		}

		pthread_t th;
		ssize_t rc;

		// Accept connection
		if (args.verbose) {
			fprintf(stderr, "%s yashd[daemon]: INFO: Accepting connections\n",
					timeStr(time_buff, BUFF_SIZE_TIMESTAMP));
		}
		// TODO: make this a non-blocking connection?
		ps  = accept(s, (struct sockaddr *)&from, &fromlen);

		// Spawn thread
		if (args.verbose) {
			fprintf(stderr, "%s yashd[daemon]: INFO: Spawning thread to handle "
					"new connection\n",
					timeStr(time_buff, BUFF_SIZE_TIMESTAMP));
		}
		th_args_t th_args;
		th_args.cmd_args = args;
		th_args.from = from;
		th_args.ps = ps;
		th_info_table[th_table_idx].my_socket = ps;
		if (args.verbose) {
			fprintf(stderr, "%s yashd[daemon]: INFO: Spawning thread to handle "
					"new connection\n",
					timeStr(time_buff, BUFF_SIZE_TIMESTAMP));
		}
		if ((rc = pthread_create(&th, NULL, serverThread, &th_args))) {
			fprintf(stderr, "%s yashd[daemon]: ERROR: pthread_create, rc: %d\n",
					timeStr(time_buff, BUFF_SIZE_TIMESTAMP), (int) rc);
			close(ps);
			return (EXIT_ERR_THREAD);
		}

		//close(ps);	// TODO: Delete this

		th_table_idx++;

		// Sleep
		sleep(MAIN_LOOP_SLEEP_TIME);

		if (args.verbose) {
			fprintf(stderr, "%s yashd[daemon]: INFO: Finished iteration in "
					"main loop\n",
					timeStr(time_buff, BUFF_SIZE_TIMESTAMP));
		}
	}

	// TODO: Ensure all threads and child processes are dead on exit

	return (EXIT_OK);
}
