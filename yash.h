/**
 * @file yash.h
 *
 * @brief Yash shell client
 *
 * Client Should connect to a port 3826 and needs host IP address to start
 *
 * @author: Utkarsh Vardan <uvardan@utexas.edu>
 * @author:	Jose Carlos Martinez Garcia-Vaso <carlosgvaso@gmail.com>
 */


#ifndef YASH_H_
#define YASH_H_


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


// Functions
void cleanBuffer(char *buffer);
static void clientSignalHandler(int sigNum);
void receiveUserInput();
int main(int argc, char **argv);


#endif /* YASH_H_ */
