/**
 * @file yash.c
 *
 * @brief Yash shell client
 *
 * Client Should connect to a port 3826 and needs host IP address to start
 *
 * @author: Utkarsh Vardan <uvardan@utexas.edu>
 * @author:	Jose Carlos Martinez Garcia-Vaso <carlosgvaso@gmail.com>
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#define MAX_HN 80
#define BUFFER_SIZE 50000
#define MAX_INPT_LEN 200


// Globals
char rbuf[BUFFER_SIZE];
char buff[BUFFER_SIZE];
ssize_t rc;
int sd;


// Functions
/**
 *
 * @param buffer
 */
void cleanBuffer(char *buffer) {
	int i;
	int size = (int) strlen(buffer) + 1;
	for (i = 0; i < size; i++)
		buffer[i] = '\0';
}


/**
 *
 * @param sigNum
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
 *
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
 *
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char **argv) {
	int child_pid;
	struct sockaddr_in server;
	//struct sockaddr_in client;
	struct hostent *h_name,* gethostbyname();
	//struct servent *s_name;
	struct sockaddr_in _from;
	struct sockaddr_in _addr;
	socklen_t _fromlen;
	//int _len;
	char thHost[80];
	uint16_t server_port = 3826;

	//s_name = getservbyname("echo", "tcp");

	gethostname(thHost, MAX_HN);

	if ((h_name = gethostbyname(thHost)) == NULL) {
		fprintf(stderr, "Invalid Host %s\n", argv[1]);
		exit(-1);
	}
	bcopy(h_name->h_addr, &(server.sin_addr), h_name->h_length);

	if (argc == 1) {
		printf("Invalid Input! Host _addr missing\n");
		exit(EXIT_FAILURE);
	}

	if ((h_name = gethostbyname(argv[1])) == NULL) {
		_addr.sin_addr.s_addr = inet_addr(argv[1]);
		if ((h_name = gethostbyaddr((char*) &_addr.sin_addr.s_addr,
				sizeof(_addr.sin_addr.s_addr), AF_INET)) == NULL) {
			fprintf(stderr, "Can't find host %s\n", argv[1]);
			exit(-1);
		}
	}
	bcopy(h_name->h_addr, &(server.sin_addr), h_name->h_length);

	server.sin_family = AF_INET;

	server.sin_port = htons(server_port);

	sd = socket(AF_INET, SOCK_STREAM, 0);

	if (sd < 0) {
		perror("Initializing the Socket Stream");
		exit(-1);
	}

	if (connect(sd, (struct sockaddr*) &server, sizeof(server)) < 0) {
		close(sd);
		perror("connecting ...");
		exit(0);
	}
	_fromlen = sizeof(_from);
	if (getpeername(sd, (struct sockaddr*) &_from, &_fromlen) < 0) {
		perror("no  peer name\n");
		exit(1);
	}
	if ((h_name = gethostbyaddr((char*) &_from.sin_addr.s_addr,
			sizeof(_from.sin_addr.s_addr), AF_INET)) == NULL)
		fprintf(stderr, "Host %s not found\n", inet_ntoa(_from.sin_addr));
	else
		child_pid = fork();
	if (child_pid == 0) {
		receiveUserInput();
	}

	// Receive data from user and dispaly it back

	signal(SIGINT, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	for (;;) {
		cleanBuffer(rbuf);
		if ((rc = recv(sd, rbuf, sizeof(rbuf), 0)) < 0) {
			perror("getting message");
			exit(-1);
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
			exit(0);
		}

	}
}
