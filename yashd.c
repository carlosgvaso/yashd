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

static th_info_t th_table[MAX_CONCURRENT_CLIENTS];	//! Thread table
static int th_table_idx = 0;						//! Number of threads in table
static pthread_mutex_t th_table_lock;				//! Thread table lock


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
	char buf_time[BUFF_SIZE_TIMESTAMP];
	int one = 1;

	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*) &one, sizeof(one))
			== -1) {
		fprintf(stderr, "%s yashd[daemon]: ERROR: error in setsockopt, "
				"SO_REUSEPORT \n", timeStr(buf_time, BUFF_SIZE_TIMESTAMP));
		exit(EXIT_ERR_SOCKET);
	}
}


/**
 * @brief Create and open server socket
 *
 * @param	port	Socket port number
 */
int createSocket(int port) {
	char buf_time[BUFF_SIZE_TIMESTAMP];
	char hostname_str[MAX_HOSTNAME_LEN];
	int sd, pn;
	socklen_t length;
	struct sockaddr_in server;
	struct hostent *hp,* gethostbyname();

	// Get host information, NAME and INET ADDRESS
	gethostname(hostname_str, MAX_HOSTNAME_LEN);
	// strcpy(ThisHost,"localhost");

	fprintf(stderr, "%s yashd[daemon]: TCP server running at hostname: %s\n",
			timeStr(buf_time, BUFF_SIZE_TIMESTAMP), hostname_str);

	if ((hp = gethostbyname(hostname_str)) == NULL) {
		fprintf(stderr, "%s yashd[daemon]: ERROR: Cannot find host %s\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP), hostname_str);
		exit(EXIT_ERR_SOCKET);
	}

	bcopy(hp->h_addr, &(server.sin_addr), hp->h_length);
	fprintf(stderr, "%s yashd[daemon]: TCP server INET ADDRESS is: %s\n",
			timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
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
			timeStr(buf_time, BUFF_SIZE_TIMESTAMP), ntohs(server.sin_port));

	// Accept TCP connections from clients
	listen(sd, MAX_CONNECT_QUEUE);

	return sd;
}


/**
 * @brief Search the thread table for the index of the given Thread ID
 * @param	tid	Thread ID
 * @return	The index of the thread, or -1 if not found
 */
int searchThByTid(pthread_t tid) {
	pthread_mutex_lock(&th_table_lock);
	for (int i=0; i<th_table_idx; i++) {
		if (th_table[i].tid == tid) {
			pthread_mutex_unlock(&th_table_lock);
			return i;
		}
	}
	pthread_mutex_unlock(&th_table_lock);

	perror("ERROR: Could not find thread in table");
	return -1;
}


/**
 * @brief Remove thread from thread table by index
 *
 * @param	idx	Index of the thread to remove in the thread table
 */
void removeThFromTableByIdx(int idx) {
	// Get semaphore to remove thread from thread table
	pthread_mutex_lock(&th_table_lock);

	// Check the index is within bounds
	if (idx < 0 || idx >= th_table_idx) {
		perror("Thread index provided not in thread table");
		return;
	}

	// Reduce the index if thread to remove is at the end of the table
	if (th_table_idx-1 == idx) {
		th_table_idx--;
	}
	th_table[idx].tid = 0;
	th_table[idx].run = false;
	th_table[idx].socket = 0;
	th_table[idx].pid = 0;
	pthread_mutex_unlock(&th_table_lock);
}


/**
 * @brief Remove thread from table by Thread ID
 * @param	tid	Thread ID
 */
void removeThFromTableByTid(pthread_t tid) {
	int th_idx = -1;

	// Search thread table and get thread index on table
	th_idx = searchThByTid(tid);

	// Check we found the thread
	if (th_idx < 0) {
		perror("ERROR: Could not remove thread from table");
		return;
	}

	// Get semaphore to remove thread from thread table
	pthread_mutex_lock(&th_table_lock);
	// Reduce the index if thread to remove is at the end of the table
	if (th_table_idx-1 == th_idx) {
		th_table_idx--;
	}
	th_table[th_idx].tid = 0;
	th_table[th_idx].run = false;
	th_table[th_idx].socket = 0;
	th_table[th_idx].pid = 0;
	pthread_mutex_unlock(&th_table_lock);
}


/**
 * @brief Release necessary resources to exit the thread safely
 */
void exitThreadSafely() {

	int th_idx = -1;

	// Search thread table and get thread index on table
	th_idx = searchThByTid(pthread_self());

	// Check we found the thread
	if (th_idx < 0) {
		perror("ERROR: Could not exit the thread safely");
		pthread_exit(NULL);
	}

	// Release thread resources
	pthread_mutex_lock(&th_table_lock);
	close(th_table[th_idx].socket);
	pthread_mutex_unlock(&th_table_lock);


	// Remove thread from table
	removeThFromTableByIdx(th_idx);

	// Exit thread
	pthread_exit(NULL);
}


/**
 * @brief Separate the message type and arguments
 *
 * If the message is malformed, this function returns an empty msg_args_t
 * struct.
 *
 * @param	msg	Raw message string
 * @return	Struct with the parsed message
 */
msg_args_t parseMessage(char *msg) {
	char buf[MAX_CMD_LEN+5];
	size_t len_orig, len_first;
	msg_args_t msg_parsed;

	// Remove final newline char and replace with NULL char
	len_orig = strlen(msg);

	if (msg[len_orig-1] == '\n') {
		msg[len_orig-1] = '\0';
	}

	// Check the message is not empty (larger than "CMD \0")
	if (len_orig <= 5) {
		strcpy(msg_parsed.type, EMPTY_STR);
		strcpy(msg_parsed.args, EMPTY_STR);
		return msg_parsed;
	}

	// Copy string so strtok() doesn't modify the original string
	strcpy(buf, msg);

	// Find and save the msg type
	// Use white space as delimiter to get the first word in the string
	char *type = strtok(buf, MSG_TYPE_DELIM);
	strcpy(msg_parsed.type, type);

	// Check there are msg args
	len_first = strlen(buf);
	if (len_first >= len_orig) {
		strcpy(msg_parsed.type, EMPTY_STR);
		strcpy(msg_parsed.args, EMPTY_STR);
		return msg_parsed;
	}

	// Find and save the msg arguments
	// Use the null char as delimiter to get the second part of the string
	char *args = strtok(NULL, MSG_ARGS_DELIM);
	strcpy(msg_parsed.args, args);

	return msg_parsed;
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
	bool run_th = true;
	char buf_time[BUFF_SIZE_TIMESTAMP];
	char buf_msg[MAX_CMD_LEN+5];	// Add space for CMD/CTL + <blank> and "\0"
	int rc;
	struct hostent *hp, *gethostbyname();
	char *prompt = CMD_PROMPT;

	// Get lock to add thread id to shared thread table
	pthread_mutex_lock(&th_table_lock);
	th_table[th_args->idx].tid = pthread_self();
	pthread_mutex_unlock(&th_table_lock);


	if (th_args->cmd_args.verbose) {
		fprintf(stderr, "%s yashd[%s:%d]: INFO: Serving client on %s:%d\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(from.sin_addr), ntohs(from.sin_port),
				inet_ntoa(from.sin_addr), ntohs(from.sin_port));
	}

	if ((hp = gethostbyaddr((char*) &from.sin_addr.s_addr,
			sizeof(from.sin_addr.s_addr), AF_INET)) == NULL) {
		if (th_args->cmd_args.verbose) {
			fprintf(stderr, "%s yashd[%s:%d]: WARN: Cannot find host: %s\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(from.sin_addr), ntohs(from.sin_port),
					inet_ntoa(from.sin_addr));
		}
	}


	// Send prompt and read messages from client
	while (run_th) {
		// Send prompt
		if (th_args->cmd_args.verbose) {
			fprintf(stderr, "%s yashd[%s:%d]: INFO: Sending prompt\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(from.sin_addr), ntohs(from.sin_port));
		}
		rc = strlen(prompt);
		if (send(ps, prompt, (size_t) rc, 0) < 0) {
			perror("ERROR: Sending stream message");
		}

		// Read client's message
		if (th_args->cmd_args.verbose) {
			fprintf(stderr, "%s yashd[%s:%d]: INFO: Reading message...\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(from.sin_addr), ntohs(from.sin_port));
		}
		if ((rc = recv(ps, buf_msg, sizeof(buf_msg), 0)) < 0) {
			perror("ERROR: Receiving stream message");
			if (th_args->cmd_args.verbose) {
				fprintf(stderr, "%s yashd[%s:%d]: ERROR: Reading message\n",
						timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
						inet_ntoa(from.sin_addr), ntohs(from.sin_port));
			}
			run_th = false;
			break;
		}

		// Check if client disconnected or handle message
		if (rc > 0) {
			buf_msg[rc] = '\0';	// Add null char to the end of the msg
			if (th_args->cmd_args.verbose) {
				fprintf(stderr, "%s yashd[%s:%d]: INFO: Message received: %s\n",
						timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
						inet_ntoa(from.sin_addr), ntohs(from.sin_port),
						buf_msg);
			}

			// Parse message
			msg_args_t msg = parseMessage(buf_msg);
			if (th_args->cmd_args.verbose) {
				fprintf(stderr, "%s yashd[%s:%d]: INFO: Message parsed %s: %s\n",
						timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
						inet_ntoa(from.sin_addr), ntohs(from.sin_port),
						msg.type, msg.args);
			}

			if (!strcmp(msg.type, MSG_TYPE_CTL)) {
				if (th_args->cmd_args.verbose) {
					fprintf(stderr, "%s yashd[%s:%d]: INFO: Signal received: "
							"%s\n",
							timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
							inet_ntoa(from.sin_addr), ntohs(from.sin_port),
							msg.args);
				}

				// TODO: Deliver signal and reply to client
				// For now we are just sending the message back
				if (send(ps, buf_msg, rc, 0) < 0) {
					perror("ERROR: Sending stream message");
				}
			} else if (!strcmp(msg.type, MSG_TYPE_CMD)) {
				fprintf(stderr, "%s yashd[%s:%d]: %s\n",
						timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
						inet_ntoa(from.sin_addr), ntohs(from.sin_port),
						msg.args);

				// TODO: Run command and reply to client
				// For now we are just sending the message back
				if (send(ps, buf_msg, rc, 0) < 0) {
					perror("ERROR: Sending stream message");
				}
			} else {
				if (th_args->cmd_args.verbose) {
					fprintf(stderr, "%s yashd[%s:%d]: ERROR: Unknown message "
							"received: %s\n",
							timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
							inet_ntoa(from.sin_addr), ntohs(from.sin_port),
							buf_msg);
				}
			}
		} else {
			if (th_args->cmd_args.verbose) {
				fprintf(stderr, "%s yashd[%s:%d]: INFO: Client disconnected\n",
						timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
						inet_ntoa(from.sin_addr), ntohs(from.sin_port));
			}
			run_th = false;	// Exit loop
			break;
		}

		// Check thread table to see if we should exit
		if (!th_table[th_args->idx].run) {
			if (th_args->cmd_args.verbose) {
				fprintf(stderr, "%s yashd[%s:%d]: INFO: Received signal to "
						"stop thread\n",
						timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
						inet_ntoa(from.sin_addr), ntohs(from.sin_port));
			}
			run_th = false;
			break;
		}
	}

	// Close resources, remove thread from the thread table and exit safely
	if (th_args->cmd_args.verbose) {
		fprintf(stderr, "%s yashd[%s:%d]: INFO: Disconnecting client...\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(from.sin_addr), ntohs(from.sin_port));
	}
	exitThreadSafely();
	pthread_exit(NULL);
}


/**
 * @brief Send signal to stop all threads and join them
 */
void stopAllThreads() {
	pthread_t tid;
	// Send signal to stop all threads, and join them afterwards
	for (int i=0; i<th_table_idx; i++) {
		// Check the entry is populated
		if (th_table[i].run) {
			tid = th_table[i].tid;
			th_table[i].run = false;	// Send stop signal
			pthread_join(tid, NULL);	// Wait for the thread to stop
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
int main(int argc, char **argv) {
	bool run = true;
	char buf_time[BUFF_SIZE_TIMESTAMP];
	int s, ps;
	socklen_t fromlen;
	struct sockaddr_in from;

	// Process command line arguments
	args = parseArgs(argc, argv);

	// Initialize the daemon
	strcpy(log_path, DAEMON_LOG_PATH);
	strcpy(pid_path, DAEMON_PID_PATH);
	daemonInit(DAEMON_DIR, DAEMON_UMASK);

	// Initialize thread table lock
	if (pthread_mutex_init(&th_table_lock, NULL) != 0) {
		fprintf(stderr, "%s yashd[daemon]: ERROR: Mutex init has failed\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP));
		exit(EXIT_ERR_THREAD);
	}

	// Set up server socket
	s = createSocket(args.port);

	// Accept connections from clients and serve them on a new thread
	fromlen = sizeof(from);
	while(run) {
		if (args.verbose) {
			fprintf(stderr, "%s yashd[daemon]: INFO: Started iteration in "
					"main loop\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP));
		}

		pthread_t th;
		ssize_t rc;

		// Accept connection
		if (args.verbose) {
			fprintf(stderr, "%s yashd[daemon]: INFO: Accepting connections\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP));
		}
		// TODO: make this a non-blocking connection?
		ps  = accept(s, (struct sockaddr *)&from, &fromlen);

		// Spawn thread to handle new connection
		if (args.verbose) {
			fprintf(stderr, "%s yashd[daemon]: INFO: Spawning thread to handle "
					"new client at %s:%d\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(from.sin_addr), ntohs(from.sin_port));
		}

		th_args_t th_args;
		th_args.cmd_args = args;
		th_args.from = from;
		th_args.ps = ps;
		th_args.idx = th_table_idx;

		// Add thread to end of table
		pthread_mutex_lock(&th_table_lock);
		th_table[th_table_idx].socket = ps;
		th_table[th_table_idx].run = true;
		th_table_idx++;
		pthread_mutex_unlock(&th_table_lock);

		if ((rc = pthread_create(&th, NULL, serverThread, &th_args))) {
			fprintf(stderr, "%s yashd[daemon]: ERROR: pthread_create, rc: %d\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP), (int) rc);

			// Release resources and exit
			close(ps);
			close(s);
			pthread_mutex_destroy(&th_table_lock);
			exit(EXIT_ERR_THREAD);
		}

		// Sleep
		//sleep(MAIN_LOOP_SLEEP_TIME);

		if (args.verbose) {
			fprintf(stderr, "%s yashd[daemon]: INFO: Finished iteration in "
					"main loop\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP));
		}
	}

	// TODO: Ensure all threads and child processes are dead on exit
	stopAllThreads();

	// Release resources and exit
	close(s);
	pthread_mutex_destroy(&th_table_lock);
	exit(EXIT_OK);
}
