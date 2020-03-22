/**
 * @file yashd.c
 *
 * @brief Yash shell daemon
 *
 * @author:	Jose Carlos Martinez Garcia-Vaso <carlosgvaso@utexas.edu>
 * @author: Utkarsh Vardan <uvardan@utexas.edu>
 */

#include "yashd.h"


// Globals
extern int errno;

static char log_path[PATHMAX+1];
static char pid_path[PATHMAX+1];

servant_th_info_t servant_th_table[MAX_CONCURRENT_CLIENTS];	//! Thread table
int servant_th_table_idx = 0;						//! New thread index in table
pthread_mutex_t servant_th_table_lock;				//! Thread table lock


/**
 * \brief Clean an array buffer by setting all entries to '\0'
 *
 * \param	buffer	Buffer
 * \param	size	Number of entries in buffer array
 */
void cleanBuffer(char *buffer, int size) {
	for (int i=0; i<size; i++) {
		buffer[i] = '\0';
	}
}


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
		cmd_args_t args = {false, DEFAULT_TCP_PORT};

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

	char buf_time[BUFF_SIZE_TIMESTAMP];
	fprintf(stderr, "%s yashd[daemon]: INFO: Stopping daemon...\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP));
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
 * \name Message Communication Protocol
 *
 * The messages sent between the server and client are expected to be
 * ASCII strings encapsulated between a start-message delimiter and a
 * end-message. The start-message delimiter is 2 bytes of value `0x02` (STX or
 * start of text ASCII control code), and the end-message delimiter is 2 bytes
 * of value `0x03` (ETX or end of text ASCII control code). We don't expect to
 * see any ASCII control codes in the messages except horizontal tabs (`0x09`)
 * and new lines (`0x0A`). See below for examples of messages:
 *
 * ```console
 * (STX)(STX)CMD ls -l(ETX)(ETX)
 * (STX)(STX)CTL c(ETX)(ETX)
 * (STX)(STX)This is text output from a command.\n\tIt can have tabs and new lines(ETX)(ETX)
 * ```
 *
 * The recvMsg() and sendMsg() functions will try to receive and send a full
 * message respectively from a non-blocking socket.
 */
///@{
/**
 * @brief Receive encapsulated message over non-blocking socket
 *
 * \param	socket	Socket file descriptor
 * \param	buffer	Buffer to store the message
 * \return	Size in bytes of the received message
 */
int recvMsg(int socket, msg_t *buffer) {
	bool receiving = false;
	size_t rc = 0;
	char buf;

	buffer->msg_size = 0;
	cleanBuffer(buffer->msg, MAX_CMD_LEN+5);	// Start with a fresh buffer

	// Read socket byte-by-byte until we get the start-message delimiter
	while (!receiving) {
		rc = recv(socket, &buf, 1, 0);

		// Check if we received anything
		if (rc < 0) {
			// Nothing to receive or error
			// TODO: handle errors
			perror("ERROR: Receiving stream message");
			return -1;
		} else if (rc == 0) {
			// Socket closed
			// TODO: disconnect client
			perror("ERROR: Receiving stream message");
			return -1;
		} else if (buf == MSG_START_DELIMITER) {
			// First start-message delimiter detected
			// Check for second delimiter
			rc = recv(socket, &buf, 1, 0);

			// Check if we received anything
			if (rc < 0) {
				// Nothing to receive or error
				// TODO: handle errors
				perror("ERROR: Receiving stream message");
				return -1;
			} else if (rc == 0) {
				// Socket closed
				// TODO: disconnect client
				perror("ERROR: Receiving stream message");
				return -1;
			} else if (buf == MSG_START_DELIMITER) {
				// Second start-message delimiter found
				receiving = true;
			}
		} else {
			// Ignore it garbage
		}
	}

	// Read socket byte-by-byte until we get the end-message delimiter
	while (receiving) {
		rc = recv(socket, &buf, 1, 0);

		// Check if we received anything
		if (rc < 0) {
			// Nothing to receive or error
			// TODO: handle errors
			return -1;
		} else if (rc == 0) {
			// Socket closed
			// TODO: disconnect client
			return -1;
		} else if (buf == MSG_END_DELIMITER) {
			// First end-message delimiter detected
			// Check for second delimiter
			rc = recv(socket, &buf, 1, 0);

			// Check if we received anything
			if (rc < 0) {
				// Nothing to receive or error
				// TODO: handle errors
				perror("ERROR: Receiving stream message");
				return -1;
			} else if (rc == 0) {
				// Socket closed
				// TODO: disconnect client
				perror("ERROR: Receiving stream message");
				return -1;
			} else if (buf == MSG_END_DELIMITER) {
				// Second end-message delimiter found
				receiving = false;
			}
		} else {
			// Save message chunk to buffer
			buffer->msg[buffer->msg_size] = buf;
			buffer->msg_size++;
		}
	}

	// Add '\0' to end of buffer
	//buffer->msg[buffer->msg_size] = '\0';

	return buffer->msg_size;
}


/**
 * \brief Send encapsulated message over non-blocking socket
 *
 * \param	socket	Socket file descriptor
 * \param	buffer	Buffer with the message
 * \param	size	Size in bytes of the message
 * \return	Size in bytes of the sent message
 */
int sendMsg(int socket, msg_t *buffer) {
	int idx;
	// Create a buffer larger than the original to fit the delimiters
	char *buf;
	buf = (char *) malloc(buffer->msg_size+4);

	// Add start-message delimiters
	buf[0] = MSG_START_DELIMITER;
	buf[1] = MSG_START_DELIMITER;

	// Add message to buffer
	for (idx=2; idx<buffer->msg_size+2; idx++) {
		buf[idx] = buffer->msg[idx-2];
	}

	// Add end-message delimiters and increase index
	buf[idx] = MSG_END_DELIMITER;
	idx++;
	buf[idx] = MSG_END_DELIMITER;
	idx++;

	idx = send(socket, buf, idx, 0);

	free(buf);

	return idx;
}
///@}


/**
 * @brief Print the servant thread table to stderr
 */
void printServantThTable() {
	char buf_time[BUFF_SIZE_TIMESTAMP];

	fprintf(stderr, "%s yashd[daemon]: INFO: Servant Thread Table:\n",
			timeStr(buf_time, BUFF_SIZE_TIMESTAMP));

	pthread_mutex_lock(&servant_th_table_lock);
	for (int i=0; i<servant_th_table_idx; i++) {
		fprintf(stderr, "\t[%d] TID: %lu, Status: %s, Socket FD: %d\n",
				i, servant_th_table[i].tid, servant_th_table[i].run ? "Running" : "Done",
				servant_th_table[i].socket);
	}
	pthread_mutex_unlock(&servant_th_table_lock);
}


/**
 * @brief Search the servant thread table for the index of the given Thread ID
 * @param	tid	Thread ID
 * @return	The index of the thread, or -1 if not found
 */
int searchServantThByTid(pthread_t tid) {
	pthread_mutex_lock(&servant_th_table_lock);
	for (int i=0; i<servant_th_table_idx; i++) {
		if (servant_th_table[i].tid == tid) {
			pthread_mutex_unlock(&servant_th_table_lock);
			return i;
		}
	}
	pthread_mutex_unlock(&servant_th_table_lock);

	perror("ERROR: Could not find servant thread in table");
	return -1;
}


/**
 * @brief Remove thread from servant thread table by index
 *
 * @param	idx	Index of the thread to remove in the thread table
 */
void removeServantThFromTableByIdx(int idx) {
	// Get semaphore to remove thread from thread table
	pthread_mutex_lock(&servant_th_table_lock);

	// Check the index is within bounds
	if (idx < 0 || idx >= servant_th_table_idx) {
		perror("Thread index provided not in servant thread table");
		pthread_mutex_unlock(&servant_th_table_lock);
		return;
	}

	// Remove thread info from table
	servant_th_table[idx].tid = 0;
	servant_th_table[idx].run = false;
	servant_th_table[idx].socket = 0;
	//th_table[idx].pid = 0;

	// Iterate over the table backwards to lower the table index
	for (int i=(servant_th_table_idx-1); i>=0; i--) {
		// Reduce the index if thread at the end of the table is done
		if (!servant_th_table[i].run) {
			servant_th_table_idx--;
		} else {	// Exit when we find the last running thread
			break;
		}
	}

	pthread_mutex_unlock(&servant_th_table_lock);
}


/**
 * @brief Remove thread from servant thread table by Thread ID
 * @param	tid	Thread ID
 */
void removeServantThFromTableByTid(pthread_t tid) {
	int th_idx = -1;

	// Get semaphore to remove thread from thread table
	pthread_mutex_lock(&servant_th_table_lock);

	// Search thread table and get thread index on table
	th_idx = searchServantThByTid(tid);

	// Check we found the thread
	if (th_idx < 0 || th_idx >= servant_th_table_idx) {
		perror("ERROR: Could not remove servant thread from table");
		pthread_mutex_unlock(&servant_th_table_lock);
		return;
	}

	pthread_mutex_unlock(&servant_th_table_lock);

	removeServantThFromTableByIdx(th_idx);
}


/**
 * @brief Send signal to stop all servant threads and join them
 */
void stopAllServantThreads() {
	pthread_t tid;
	// Send signal to stop all threads, and join them afterwards
	for (int i=(servant_th_table_idx-1); i>=0; i--) {
		// Check the entry is populated
		if (servant_th_table[i].run) {
			tid = servant_th_table[i].tid;
			pthread_mutex_lock(&servant_th_table_lock);
			servant_th_table[i].run = false;	// Send stop signal
			pthread_mutex_unlock(&servant_th_table_lock);
			pthread_join(tid, NULL);	// Wait for the thread to stop
		}
	}
}


/**
 * @brief Release necessary resources to exit the servant thread safely
 */
void exitServantThreadSafely() {
	int th_idx = -1;

	// Search thread table and get thread index on table
	th_idx = searchServantThByTid(pthread_self());

	// Check we found the thread
	if (th_idx < 0 || th_idx >= servant_th_table_idx) {
		perror("ERROR: Could not exit the servant thread safely");
		pthread_exit(NULL);
	}

	// Release thread resources
	pthread_mutex_lock(&servant_th_table_lock);
	close(servant_th_table[th_idx].socket);
	pthread_mutex_unlock(&servant_th_table_lock);


	// Remove thread from table
	removeServantThFromTableByIdx(th_idx);

	// Exit thread
	pthread_exit(NULL);
}


/**
 * \brief Print the job thread table to stderr
 *
 * \param	shell_info	Shell info struct
 */
void printJobThTable(shell_info_t *shell_info) {
	char buf_time[BUFF_SIZE_TIMESTAMP];

	fprintf(stderr, "%s yashd[daemon]: INFO: Job Thread Table:\n",
			timeStr(buf_time, BUFF_SIZE_TIMESTAMP));

	pthread_mutex_lock(&shell_info_lock);
	for (int i=0; i<shell_info->job_th_table_idx; i++) {
		fprintf(stderr, "\t[%d] TID: %lu, Status: %s, Job no: %d\n",
				i, shell_info->job_th_table[i].tid,
				shell_info->job_th_table[i].run ? "Running" : "Done",
				shell_info->job_th_table[i].jobno);
	}
	pthread_mutex_unlock(&shell_info_lock);
}


/**
 * \brief Search the job thread table for the index of the given Thread ID
 *
 * \param	tid	Thread ID
 * \param	shell_info	Shell info struct
 * \return	The index of the thread, or -1 if not found
 */
int searchJobThByTid(pthread_t tid, shell_info_t *shell_info) {
	pthread_mutex_lock(&shell_info_lock);
	for (int i=0; i<shell_info->job_th_table_idx; i++) {
		if (shell_info->job_th_table[i].tid == tid) {
			pthread_mutex_unlock(&shell_info_lock);
			return i;
		}
	}
	pthread_mutex_unlock(&shell_info_lock);

	perror("ERROR: Could not find job thread in table");
	return -1;
}


/**
 * \brief Remove thread from job thread table by index
 *
 * \param	idx	Index of the thread to remove in the thread table
 * \param	shell_info	Shell info struct
 */
void removeJobThFromTableByIdx(int idx, shell_info_t *shell_info) {
	// Get semaphore to remove thread from thread table
	pthread_mutex_lock(&shell_info_lock);

	// Check the index is within bounds
	if (idx < 0 || idx >= shell_info->job_th_table_idx) {
		perror("Thread index provided not in job thread table");
		pthread_mutex_unlock(&shell_info_lock);
		return;
	}

	// Remove thread info from table
	shell_info->job_th_table[idx].tid = 0;
	shell_info->job_th_table[idx].run = false;
	shell_info->job_th_table[idx].jobno = 0;
	//shell_info->job_th_table[idx].socket = 0;
	//shell_info->job_th_table[idx].pid = 0;

	// Iterate over the table backwards to lower the table index
	for (int i=shell_info->job_th_table_idx-1; i>=0; i--) {
		// Reduce the index if thread at the end of the table is done
		if (!shell_info->job_th_table[i].run) {
			shell_info->job_th_table_idx--;
		} else {	// Exit when we find the last running thread
			break;
		}
	}

	pthread_mutex_unlock(&shell_info_lock);
}


/**
 * \brief Remove thread from job thread table by Thread ID
 *
 * \param	tid	Thread ID
 * \param	shell_info	Shell info struct
 */
void removeJobThFromTableByTid(pthread_t tid, shell_info_t *shell_info) {
	int th_idx = -1;

	// Get semaphore to remove thread from thread table
	pthread_mutex_lock(&shell_info_lock);

	// Search thread table and get thread index on table
	th_idx = searchJobThByTid(tid, shell_info);

	// Check we found the thread
	if (th_idx < 0 || th_idx >= shell_info->job_th_table_idx) {
		perror("ERROR: Could not remove thread from job thread table");
		pthread_mutex_unlock(&shell_info_lock);
		return;
	}

	pthread_mutex_unlock(&shell_info_lock);

	removeJobThFromTableByIdx(th_idx, shell_info);
}


/**
 * \brief Send signal to stop all job threads and join them
 *
 * \param	shell_info	Shell info struct
 */
void stopAllJobThreads(shell_info_t *shell_info) {
	pthread_t tid;
	// Send signal to stop all threads, and join them afterwards
	for (int i=(shell_info->job_th_table_idx-1); i>=0; i--) {
		// Check the entry is populated
		if (shell_info->job_th_table[i].run) {
			tid = shell_info->job_th_table[i].tid;
			pthread_mutex_lock(&shell_info_lock);
			shell_info->job_th_table[i].run = false;	// Send stop signal
			pthread_mutex_unlock(&shell_info_lock);
			pthread_join(tid, NULL);	// Wait for the thread to stop
		}
	}
}


/**
 * \brief Release necessary resources to exit the job thread safely
 *
 * \param	shell_info	Shell info struct
 */
void exitJobThreadSafely(shell_info_t *shell_info) {
	int th_idx = -1;

	// Search thread table and get thread index on table
	th_idx = searchJobThByTid(pthread_self(), shell_info);

	// Check we found the thread
	if (th_idx < 0 || th_idx >= shell_info->job_th_table_idx) {
		perror("ERROR: Could not exit the job thread safely");
		pthread_exit(NULL);
	}

	// Release thread resources
	//pthread_mutex_lock(shell_info_lock);
	//close(shell_info->job_th_table[th_idx].socket);
	//pthread_mutex_unlock(shell_info_lock);


	// Remove thread from table
	removeJobThFromTableByIdx(th_idx, shell_info);

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
 * \brief Handle CTL messages
 *
 * Message arguments supported:
 * 	- c: SIGINT
 * 	- z: SIGTSTP
 * 	- d: EOF (disconnect client)
 *
 * \param	arg				CTL message argument
 * \param	shell_info		Shell info struct pointer
 */
void handleCTLMessages(char arg, shell_info_t *shell_info) {
	char buf_time[BUFF_SIZE_TIMESTAMP];
	pid_t pid_job = 0;

	pthread_mutex_lock(&shell_info_lock);

	// Search the job table to see if we have a fg job
	for (int i=((shell_info->job_table_idx)-1); i>=0; i--) {
		if (strcmp(shell_info->job_table[i].status, JOB_STATUS_DONE) &&
				!shell_info->job_table[i].bg) {
			pid_job = shell_info->job_table[i].gpid;
			break;
		}
	}

	if (pid_job == 0) {
		if (arg == MSG_CTL_EOF) {
			if (args.verbose) {
				fprintf(stderr, "%s yashd[%s:%d]: INFO: EOF received\n",
						timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
						inet_ntoa(shell_info->th_args.from.sin_addr),
						ntohs(shell_info->th_args.from.sin_port));
				fprintf(stderr, "%s yashd[%s:%d]: INFO: Disconnecting client...\n",
						timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
						inet_ntoa(shell_info->th_args.from.sin_addr),
						ntohs(shell_info->th_args.from.sin_port));
			}
			pthread_mutex_unlock(&shell_info_lock);
			exitServantThreadSafely();
			return;
		} else {
			fprintf(stderr, "%s yashd[%s:%d]: INFO: No foreground "
					"process to receive the signal\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(shell_info->th_args.from.sin_addr),
					ntohs(shell_info->th_args.from.sin_port));
			pthread_mutex_unlock(&shell_info_lock);
			return;
		}
	}

	switch(arg) {
	case MSG_CTL_SIGINT:
		// Send SIGINT to child process
		if (args.verbose) {
			fprintf(stderr, "%s yashd[%s:%d]: INFO: Sending SIGINT to "
					"child process\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(shell_info->th_args.from.sin_addr),
					ntohs(shell_info->th_args.from.sin_port));
		}
		kill(pid_job, SIGINT);
		break;
	case MSG_CTL_SIGTSTP:
		// Send SIGTSTP to child process
		if (args.verbose) {
			fprintf(stderr, "%s yashd[%s:%d]: INFO: Sending SIGTSTP to "
					"child process\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(shell_info->th_args.from.sin_addr),
					ntohs(shell_info->th_args.from.sin_port));
		}
		kill(pid_job, SIGTSTP);
		break;
	case MSG_CTL_EOF:
		// Disconnect from client
		// Close resources, remove thread from the thread table and exit safely
		if (args.verbose) {
			fprintf(stderr, "%s yashd[%s:%d]: INFO: EOF received\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(shell_info->th_args.from.sin_addr),
					ntohs(shell_info->th_args.from.sin_port));
			fprintf(stderr, "%s yashd[%s:%d]: INFO: Disconnecting client...\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(shell_info->th_args.from.sin_addr),
					ntohs(shell_info->th_args.from.sin_port));
		}
		pthread_mutex_unlock(&shell_info_lock);
		exitServantThreadSafely();
		break;
	default:
		fprintf(stderr, "%s yashd[%s:%d]: ERROR: Unknown CTL message argument "
				"received: %c\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(shell_info->th_args.from.sin_addr),
				ntohs(shell_info->th_args.from.sin_port), arg);
	}
	pthread_mutex_unlock(&shell_info_lock);
}


/**
 * \brief Execute job in a separate thread
 *
 * \param	job_thread_args	JOb thread arguments struct pointer
 */
void *jobThread(void *job_thread_args) {
	// Save job th args struct to local variable
	job_thread_args_t *j_th_args = (job_thread_args_t *) job_thread_args;
	job_thread_args_t job_th_args_l;
	strcpy(job_th_args_l.args, j_th_args->args);
	job_th_args_l.job_th_idx = j_th_args->job_th_idx;
	job_th_args_l.shell_info = j_th_args->shell_info;
	job_thread_args_t *job_th_args = &job_th_args_l;
	//pthread_mutex_lock(&shell_info_lock);
	bool verbose = args.verbose;
	//pthread_mutex_unlock(&shell_info_lock);
	char buf_time[BUFF_SIZE_TIMESTAMP];
	int rc = 0;
	char *prompt = CMD_PROMPT;

	if (verbose) {
		fprintf(stderr, "%s yashd[%s:%d]: INFO: Starting job thread for: %s\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(job_th_args->shell_info->th_args.from.sin_addr),
				ntohs(job_th_args->shell_info->th_args.from.sin_port),
				job_th_args->args);
	}

	// Start job
	startJob(job_th_args->args, job_th_args->shell_info);


	// Send prompt
	if (verbose) {
		fprintf(stderr, "%s yashd[%s:%d]: INFO: Sending prompt\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(job_th_args->shell_info->th_args.from.sin_addr),
				ntohs(job_th_args->shell_info->th_args.from.sin_port));
	}
	rc = strlen(prompt);
	if (send(job_th_args->shell_info->th_args.ps, prompt, (size_t) rc, 0) < 0) {
		perror("ERROR: Sending stream message");
	}

	if (verbose) {
		fprintf(stderr, "%s yashd[%s:%d]: INFO: Stopping job thread for: %s\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(job_th_args->shell_info->th_args.from.sin_addr),
				ntohs(job_th_args->shell_info->th_args.from.sin_port),
				job_th_args->args);
	}

	exitJobThreadSafely(job_th_args->shell_info);
	pthread_exit(NULL);
}


/**
 * \brief Handle CMD messages
 *
 * \param	args			CMD message arguments pointer
 * \param	shell_info		Shell information struct pointer
 */
void handleCMDMessages(char *arguments, shell_info_t *shell_info) {
	char buf_time[BUFF_SIZE_TIMESTAMP];

	// Implement running the job received
	if (args.verbose) {
		fprintf(stderr, "%s yashd[%s:%d]: INFO: Running job: %s\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(shell_info->th_args.from.sin_addr),
				ntohs(shell_info->th_args.from.sin_port), arguments);
	}

	// Start job thread
	int rc;
	pthread_t th_job;
	job_thread_args_t job_th_args;
	strcpy(job_th_args.args, arguments);
	job_th_args.job_th_idx = shell_info->job_th_table_idx;
	job_th_args.shell_info = shell_info;

	// Add thread to end of thread table
	pthread_mutex_lock(&shell_info_lock);
	shell_info->job_th_table[shell_info->job_th_table_idx].run = true;
	//pthread_mutex_unlock(&shell_info_lock);

	if ((rc = pthread_create(&th_job, NULL, jobThread, &job_th_args))) {
		fprintf(stderr, "%s yashd[%s:%d]: ERROR: stdinThread pthread_create "
				"failed, rc: %d\n", timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(shell_info->th_args.from.sin_addr),
				ntohs(shell_info->th_args.from.sin_port),
				(int)rc);
		fprintf(stderr, "%s yashd[%s:%d]: ERROR: Could not run job\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(shell_info->th_args.from.sin_addr),
				ntohs(shell_info->th_args.from.sin_port));
		return;
	}

	// Add thread's TID
	//pthread_mutex_lock(&shell_info_lock);
	shell_info->job_th_table[shell_info->job_th_table_idx].tid = th_job;
	shell_info->job_th_table[shell_info->job_th_table_idx].jobno =
			shell_info->job_table_idx+1;
	shell_info->job_th_table_idx++;
	pthread_mutex_unlock(&shell_info_lock);

	// Print thread table
	if (args.verbose) {
		printJobThTable(shell_info);
	}

	// Start job
	//startJob(args, shell_info);

	// Stop and join job thread
	//in_th_args.run = false;
	//pthread_join(th_in, NULL);
}


/**
 * @brief Thread function to serve the clients
 *
 * TODO: Make threads use async socket I/O
 *
 * @param	thread_args	Arguments passed to the thread as a th_args_t struct
 */
void *servantThread(void *thread_args) {
	servant_th_args_t th_args_l = *(servant_th_args_t *) thread_args;	// Save to local var
	servant_th_args_t *th_args = &th_args_l;
	int ps = th_args->ps;
	struct sockaddr_in from = th_args->from;
	bool run_serv = true;
	char buf_time[BUFF_SIZE_TIMESTAMP];
	char buf_msg[MAX_CMD_LEN+5];	// Add space for CMD/CTL + <blank> and "\0"
	int rc;
	struct hostent *hp, *gethostbyname();
	char *prompt = CMD_PROMPT;
	struct pollfd pollfds[1];
	shell_info_t sh_info;

	pollfds[0].fd = ps;
	pollfds[0].events = POLLIN;

	sh_info.th_args.cmd_args.verbose = th_args_l.cmd_args.verbose;
	sh_info.th_args.cmd_args.port = th_args_l.cmd_args.port;
	sh_info.th_args.idx = th_args_l.idx;
	sh_info.th_args.ps = th_args_l.ps;
	sh_info.th_args.from = th_args_l.from;
	sh_info.job_table_idx = 0;
	sh_info.job_th_table_idx = 0;
	if (pipe(sh_info.stdin_pipe_fd) == SYSCALL_RETURN_ERR) {
		fprintf(stderr, "%s yashd[%s:%d]: ERROR: Could not create stdin pipe: %d\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(from.sin_addr), ntohs(from.sin_port), errno);
		pthread_exit(NULL);
	}



	// Initialize job thread table lock
	if (pthread_mutex_init(&shell_info_lock, NULL) != 0) {
		fprintf(stderr, "%s yashd[%s:%d]: ERROR: Mutex init has failed\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(from.sin_addr), ntohs(from.sin_port));
		pthread_exit(NULL);
	}

	if (args.verbose) {
		fprintf(stderr, "%s yashd[%s:%d]: INFO: Serving client on %s:%d\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(from.sin_addr), ntohs(from.sin_port),
				inet_ntoa(from.sin_addr), ntohs(from.sin_port));
	}

	if ((hp = gethostbyaddr((char*) &from.sin_addr.s_addr,
			sizeof(from.sin_addr.s_addr), AF_INET)) == NULL) {
		if (args.verbose) {
			fprintf(stderr, "%s yashd[%s:%d]: WARN: Cannot find host: %s\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(from.sin_addr), ntohs(from.sin_port),
					inet_ntoa(from.sin_addr));
		}
	}

	// Send prompt
	if (args.verbose) {
		fprintf(stderr, "%s yashd[%s:%d]: INFO: Sending prompt\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(from.sin_addr), ntohs(from.sin_port));
	}
	rc = strlen(prompt);
	if (send(ps, prompt, (size_t) rc, 0) < 0) {
		perror("ERROR: Sending stream message");
	}

	// Read messages from client
	while (run_serv) {
		// Check if there is a message to read
		/*
		if (th_args->cmd_args.verbose) {
			fprintf(stderr, "%s yashd[%s:%d]: INFO: Checking if we received messages...\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(from.sin_addr), ntohs(from.sin_port));
		}
		*/
		poll(pollfds, 1, 500);	// Poll for 0.5sec
		if (pollfds[0].revents & POLLIN) {	// There is stuff to read
			pollfds[0].revents = 0;

			// Read client's message
			if (args.verbose) {
				fprintf(stderr, "%s yashd[%s:%d]: INFO: Reading message...\n",
						timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
						inet_ntoa(from.sin_addr), ntohs(from.sin_port));
			}
			if ((rc = recv(ps, buf_msg, sizeof(buf_msg), 0)) < 0) {
				perror("ERROR: Receiving stream message");
				if (args.verbose) {
					fprintf(stderr, "%s yashd[%s:%d]: ERROR: Reading message\n",
							timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
							inet_ntoa(from.sin_addr), ntohs(from.sin_port));
				}
				run_serv = false;
				break;
			}

			// Check if client disconnected or handle message
			if (rc > 0) {
				buf_msg[rc] = '\0';	// Add null char to the end of the msg
				if (args.verbose) {
					fprintf(stderr, "%s yashd[%s:%d]: INFO: Message received: %s",
							timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
							inet_ntoa(from.sin_addr), ntohs(from.sin_port),
							buf_msg);
				}

				// Parse message
				msg_args_t msg = parseMessage(buf_msg);
				if (args.verbose) {
					fprintf(stderr, "%s yashd[%s:%d]: INFO: Message parsed %s: "
							"%s\n",
							timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
							inet_ntoa(from.sin_addr), ntohs(from.sin_port),
							msg.type, msg.args);
				}

				/*
				 * TODO: Check if there is a foreground process running. If
				 * there is not the message must be of type CMD, or it is
				 * garbage. If there is, the message can be of type CTL or stdin
				 * for the foreground process. Hnadle the message appropriately.
				 */
				if (!strcmp(msg.type, MSG_TYPE_CMD)) {
					// Refresh the pipe
					pthread_mutex_lock(&shell_info_lock);
					close(sh_info.stdin_pipe_fd[0]);
					close(sh_info.stdin_pipe_fd[1]);
					if (pipe(sh_info.stdin_pipe_fd) == SYSCALL_RETURN_ERR) {
						fprintf(stderr, "%s yashd[%s:%d]: ERROR: Could not refresh stdin pipe: %d\n",
								timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
								inet_ntoa(from.sin_addr), ntohs(from.sin_port), errno);
						pthread_exit(NULL);
					}
					pthread_mutex_unlock(&shell_info_lock);

					// Handle CMD messages
					handleCMDMessages(msg.args, &sh_info);
					fprintf(stderr, "%s yashd[%s:%d]: %s\n",
							timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
							inet_ntoa(from.sin_addr), ntohs(from.sin_port),
							msg.args);
				} else if (!strcmp(msg.type, MSG_TYPE_CTL)) {
					if (args.verbose) {
						fprintf(stderr, "%s yashd[%s:%d]: INFO: Signal received: "
								"%s\n",
								timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
								inet_ntoa(from.sin_addr), ntohs(from.sin_port),
								msg.args);
					}

					// Handle CTL messages
					handleCTLMessages(msg.args[0], &sh_info);

					// Send prompt
					if (args.verbose) {
						fprintf(stderr, "%s yashd[%s:%d]: INFO: Sending prompt\n",
								timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
								inet_ntoa(from.sin_addr), ntohs(from.sin_port));
					}
					rc = strlen(prompt);
					if (send(ps, prompt, (size_t) rc, 0) < 0) {
						perror("ERROR: Sending stream message");
					}
				} else {	// This is input to be sent to the stdin pipe
					/*
					if (args.verbose) {
						fprintf(stderr, "%s yashd[%s:%d]: ERROR: Unknown message "
								"received: %s\n",
								timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
								inet_ntoa(from.sin_addr), ntohs(from.sin_port),
								buf_msg);
					}
					*/
					/*
					if (write(sh_info.stdin_pipe_fd[1], buf_msg, rc)) {
						perror("ERROR: Sending input to stdin pipe");
					}
					*/
				}
			} else {
				if (args.verbose) {
					fprintf(stderr, "%s yashd[%s:%d]: INFO: Client disconnected\n",
							timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
							inet_ntoa(from.sin_addr), ntohs(from.sin_port));
				}
				run_serv = false;	// Exit loop
				break;
			}
		} else if (pollfds[0].revents & POLLHUP) {	// Client hanged up
			if (args.verbose) {
				fprintf(stderr, "%s yashd[%s:%d]: INFO: Client disconnected\n",
						timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
						inet_ntoa(from.sin_addr), ntohs(from.sin_port));
			}
			run_serv = false;	// Exit loop
			break;
		}

		// Check thread table to see if we should exit
		/*
		if (th_args->cmd_args.verbose) {
			fprintf(stderr, "%s yashd[%s:%d]: INFO: Checking if thread should exit...\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(from.sin_addr), ntohs(from.sin_port));
		}
		*/
		if (!servant_th_table[th_args_l.idx].run) {
			if (args.verbose) {
				fprintf(stderr, "%s yashd[%s:%d]: INFO: Received signal to "
						"stop thread\n",
						timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
						inet_ntoa(from.sin_addr), ntohs(from.sin_port));
			}
			run_serv = false;	// Exit loop
			break;
		}
	}

	// Close resources, remove thread from the thread table and exit safely
	if (args.verbose) {
		fprintf(stderr, "%s yashd[%s:%d]: INFO: Disconnecting client...\n",
				timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(from.sin_addr), ntohs(from.sin_port));
	}

	/* TODO: Ensure all child processes are dead on exit
	 * killAllJobs();
	 */

	exitServantThreadSafely();
	pthread_exit(NULL);
}


/**
 * @brief Point of entry
 *
 * @param	argc	Number of command line arguments
 * @param	argv	Array of command line arguments
 * @return	Error code
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
	if (pthread_mutex_init(&servant_th_table_lock, NULL) != 0) {
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

		ps  = accept(s, (struct sockaddr *)&from, &fromlen);

		// Put the new socket into non-blocking mode
		//fcntl(ps, F_SETFL, O_NONBLOCK);

		// Spawn thread to handle new connection
		if (args.verbose) {
			fprintf(stderr, "%s yashd[daemon]: INFO: Spawning thread to handle "
					"new client at %s:%d\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(from.sin_addr), ntohs(from.sin_port));
		}

		servant_th_args_t th_args;
		th_args.cmd_args.verbose = args.verbose;
		th_args.cmd_args.port = args.port;
		th_args.from = from;
		th_args.ps = ps;
		th_args.idx = servant_th_table_idx;

		// Add thread to end of thread table
		pthread_mutex_lock(&servant_th_table_lock);
		servant_th_table[servant_th_table_idx].run = true;
		servant_th_table[servant_th_table_idx].socket = ps;

		// Create new thread
		if ((rc = pthread_create(&th, NULL, servantThread, &th_args))) {
			fprintf(stderr, "%s yashd[daemon]: ERROR: serverThread "
					"pthread_create failed, rc: %d\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					(int)rc);

			// Release resources and exit
			close(ps);
			close(s);
			pthread_mutex_unlock(&servant_th_table_lock);
			pthread_mutex_destroy(&servant_th_table_lock);
			exit(EXIT_ERR_THREAD);
		}

		// Add thread's TID
		servant_th_table[servant_th_table_idx].tid = th;
		servant_th_table_idx++;
		pthread_mutex_unlock(&servant_th_table_lock);

		// Sleep
		//sleep(MAIN_LOOP_SLEEP_TIME);

		// Print thread table
		if (args.verbose) {
			printServantThTable();
		}

		if (args.verbose) {
			fprintf(stderr, "%s yashd[daemon]: INFO: Finished iteration in "
					"main loop\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP));
		}
	}

	// Ensure all threads and child processes are dead on exit
	// TODO: This might not be needed since the threads are killed when the
	// calling main function returns
	stopAllServantThreads();

	// Release resources and exit
	close(s);
	pthread_mutex_destroy(&servant_th_table_lock);
	exit(EXIT_OK);
}
