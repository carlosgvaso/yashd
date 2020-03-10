/**
 * @file yash.c
 *
 * @brief Yash shell client
 *
 * Client Should connect to a port 3826 and needs host IP address to start
 *
 * @author: Utkarsh Vardan <uvardan@utexas.edu>
 * @author:	Jose Carlos Martinez Garcia-Vaso <carlosgvaso@utexas.edu>
 */

#include "yash.h"


// Globals
static cmd_args_t args;

char rbuf[BUFFER_SIZE];
char buff[BUFFER_SIZE];

ssize_t rc;
int sd;


// Functions


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
			"./yashd [options] <host>\n"
			"\n"
			"Required arguments:\n"
			"    host                    Yashd server host address\n"
			"\n"
			"Options:\n"
			"    -h, --help              Print help and exit\n"
			"    -p PORT, --port PORT    Server port [1024-65535]\n";
	const char ARG_ERROR[MAX_ERROR_LEN] = "-yashd: wrong number of arguments\n";
	const char H_FLAG_SHORT[3] = "-h\0";
	const char H_FLAG_LONG[10] = "--help\0";
	const char P_FLAG_SHORT[3] = "-p\0";
	const char P_FLAG_LONG[10] = "--port\0";
	const char P_INFO[MAX_ERROR_LEN] = "-yashd: using port: %d\n";
	const char P_ERROR1[MAX_ERROR_LEN] = "-yashd: missing port number\n";
	const char P_ERROR2[MAX_ERROR_LEN] = "-yashd: port must be an integer "
			"between %d and %d\n";
	cmd_args_t args = {EMPTY_STR, DEFAULT_TCP_PORT};

	// Check we got the correct number of arguments
	if (argc < 2 || argc > 4) {
		printf(ARG_ERROR);
		printf(USAGE);
		exit(EXIT_ERR_ARG);
	}

	// Loop over the arguments, skipping the command token
	for (int i=1; i<argc; i++) {
		if (!strcmp(H_FLAG_SHORT, argv[i])
				|| !strcmp(H_FLAG_LONG, argv[i])) {
			printf(USAGE);
			exit(EXIT_OK);
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
		} else { // Assume this is the host address
			strcpy(args.host, argv[i]);
		}
	}

	return args;
}


/**
 * @brief Delete data in string buffer
 *
 * @param	buffer	String buffer
 */
void cleanBuffer(char *buffer) {
	int i;
	int size = (int) strlen(buffer) + 1;
	for (i = 0; i < size; i++)
		buffer[i] = '\0';
}


/**
 * @brief Handle signals
 *
 * @param	sigNum	Signal to handle
 */
static void clientSignalHandler(int sigNum) {
	if (sigNum == SIGINT) {
		cleanBuffer(buff);
		strcpy(buff, "CTL c\n");
		rc = strlen(buff);
		if (send(sd, buff, rc, 0) < 0)
			perror("Send Msg");
		cleanBuffer(buff);
	}

	if (sigNum == SIGTSTP) {
		cleanBuffer(buff);
		strcpy(buff, "CTL z\n");
		rc = strlen(buff);
		if (send(sd, buff, rc, 0) < 0)
			perror("Send Msg");
		cleanBuffer(buff);
	}
}


/**
 * @brief Read user input from terminal, and send it to the yashd server
 */
void receiveUserInput() {

	if (signal(SIGTSTP, clientSignalHandler) == SIG_ERR)
		printf("SIGSTP error");
	if (signal(SIGINT, clientSignalHandler) == SIG_ERR)
		printf("SIGINT error");
	char *yashProtoBuf = malloc(sizeof(char) * MAX_INPT_LEN);
	char inputCmd[] = "CMD ";
	for (;;) {
		cleanBuffer(buff);
		cleanBuffer(yashProtoBuf);
		if ((rc = read(0, buff, sizeof(buff)))) {
			if (strstr(buff, "exit")) {
				break;
			}
			if (rc > 0) {
				strcat(yashProtoBuf, inputCmd);
				strcat(yashProtoBuf, buff);
				rc = strlen(yashProtoBuf);
				if (send(sd, yashProtoBuf, (size_t) rc, 0) < 0)
					perror("Sending Message");
			}
		}
		if (rc == 0) {
			break;
		}
	}
	close(sd);
	kill(getppid(), 9);
	exit(0);
}


/**
 * @brief Point of entry
 *
 * @param	argc	Number of command line arguments
 * @param	argv	Array of command line arguments
 * @return	Error code
 */
int main(int argc, char **argv) {
	int child_pid;
	struct sockaddr_in server;
	struct hostent *h_name,* gethostbyname();
	struct sockaddr_in _from;
	struct sockaddr_in _addr;
	socklen_t _fromlen;
	char thHost[MAX_HOSTNAME_LEN];

	//uint16_t server_port = 3826;
	//struct sockaddr_in client;
	//struct servent *s_name;
	//int _len;
	//s_name = getservbyname("echo", "tcp");

	// Process command line arguments
	args = parseArgs(argc, argv);

	gethostname(thHost, MAX_HOSTNAME_LEN);

	if ((h_name = gethostbyname(thHost)) == NULL) {
		fprintf(stderr, "Invalid Host %s\n", args.host);
		exit(EXIT_ERR_SOCKET);
	}
	bcopy(h_name->h_addr, &(server.sin_addr), h_name->h_length);

	if ((h_name = gethostbyname(args.host)) == NULL) {
		_addr.sin_addr.s_addr = inet_addr(args.host);
		if ((h_name = gethostbyaddr((char*) &_addr.sin_addr.s_addr,
				sizeof(_addr.sin_addr.s_addr), AF_INET)) == NULL) {
			fprintf(stderr, "Can't find host %s\n", args.host);
			exit(EXIT_ERR_SOCKET);
		}
	}
	bcopy(h_name->h_addr, &(server.sin_addr), h_name->h_length);

	server.sin_family = AF_INET;

	server.sin_port = htons(args.port);

	sd = socket(AF_INET, SOCK_STREAM, 0);

	if (sd < 0) {
		perror("Initializing the Socket Stream");
		exit(EXIT_ERR_SOCKET);
	}

	if (connect(sd, (struct sockaddr*) &server, sizeof(server)) < 0) {
		close(sd);
		perror("connecting ...");
		exit(EXIT_ERR_SOCKET);
	}
	_fromlen = sizeof(_from);
	if (getpeername(sd, (struct sockaddr*) &_from, &_fromlen) < 0) {
		perror("no  peer name\n");
		exit(EXIT_ERR_SOCKET);
	}
	if ((h_name = gethostbyaddr((char*) &_from.sin_addr.s_addr,
			sizeof(_from.sin_addr.s_addr), AF_INET)) == NULL)
		fprintf(stderr, "Host %s not found\n", inet_ntoa(_from.sin_addr));
	else
		child_pid = fork();
	if (child_pid == 0) {
		receiveUserInput();
	}

	// Receive data from user and display it back

	signal(SIGINT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	for (;;) {
		cleanBuffer(rbuf);
		if ((rc = recv(sd, rbuf, sizeof(rbuf), 0)) < 0) {
			perror("getting message");
			exit(EXIT_ERR_SOCKET);
		}
		if (strncmp(rbuf, "\n#", 2) == 0) {
			printf("%s", rbuf);
			fflush(stdout);
		} else if (rc > 0) {
			rbuf[rc] = '\0';
			printf("%s\n", rbuf);
		} else {
			printf("Disconnected!\n");
			close(sd);
			exit(EXIT_OK);
		}
	}
	return EXIT_OK;
}
