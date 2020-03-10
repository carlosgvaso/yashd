/**
 * @file  yashd.h
 *
 * @brief Yash shell daemon
 *
 * @author:	Jose Carlos Martinez Garcia-Vaso <carlosgvaso@utexas.edu>
 * @author: Utkarsh Vardan <uvardan@utexas.edu>
 */

#ifndef YASHD_DEFS_H_
#define YASHD_DEFS_H_


#define MAX_CMD_LEN			2000	//! Max command length as per requirements
#define MAX_TOKEN_LEN		30		//! Max token length as per requirements
#define MAX_ERROR_LEN		256		//! Max error message length
#define MAX_HOSTNAME_LEN	80		//! Max hostname length

#define DEFAULT_TCP_PORT	3826	//! Default daemon TCP server port
#define TCP_PORT_LOWER_LIM	1024	//! Lowest TCP port allowed
#define TCP_PORT_HIGHER_LIM	65535	//! Highest TCP port allowed

#define EMPTY_STR "\0"
#define EMPTY_ARRAY -1

#define EXIT_OK			0	//! No error
#define EXIT_ERR		1	//! Unknown error
#define EXIT_ERR_ARG	2	//! Wrong argument provided
#define EXIT_ERR_DAEMON	3	//! Daemon process error
#define EXIT_ERR_SOCKET	4	//! Socket error
#define EXIT_ERR_THREAD	5	//! Thread error
#define EXIT_ERR_CMD	6	//! Command syntax error


#endif /* YASHD_DEFS_H_ */
