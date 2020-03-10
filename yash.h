/**
 * @file yash.h
 *
 * @brief Yash shell client
 *
 * Client Should connect to a port 3826 and needs host IP address to start
 *
 * @author: Utkarsh Vardan <uvardan@utexas.edu>
 * @author:	Jose Carlos Martinez Garcia-Vaso <carlosgvaso@utexas.edu>
 */

#ifndef YASH_H_
#define YASH_H_


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include "yashd_defs.h"


#define BUFFER_SIZE 50000
#define MAX_INPT_LEN 200


/**
 * @brief Struct to organize all the command line arguments.
 *
 * Arguments:
 *   - verbose: enable debugging log output
 *   - port: port of the TCP server
 */
typedef struct _cmd_args_t {
	char host[MAX_HOSTNAME_LEN];	// Host address
	int port;						// Server port
} cmd_args_t;


// Functions
bool isNumber(char number[]);
cmd_args_t parseArgs(int argc, char** argv);
void cleanBuffer(char *buffer);
static void clientSignalHandler(int sigNum);
void receiveUserInput();
int main(int argc, char **argv);


#endif /* YASH_H_ */
