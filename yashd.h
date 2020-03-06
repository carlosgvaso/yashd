/**
 * @file  yashd.h
 *
 * @brief Main functionality of the YASH shell.
 *
 * @author:	Jose Carlos Martinez Garcia-Vaso
 */

#ifndef YASHD_H
#define YASHD_H


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <readline/readline.h>
#include <readline/history.h>

#define PATHMAX 255			//! Max length of a path
#define MAX_HOSTNAME_LEN 80	//! Max hostname length
#define MAX_CONNECT_QUEUE 5	//! Max queue of pending connections
#define MAX_CMD_LEN 2000	//! Max command length as per requirements
#define MAX_TOKEN_LEN 30	//! Max token length as per requirements
#define MAX_ERROR_LEN 256	//! Max error message length
#define MAX_STATUS_LEN 8	//! Max status string length
/**
 * @brief Max number of tokens per command.
 *
 * This is calculated as follows:
 *
 * \f[
 * \dfrac{MAX_CMD_LEN}{2} = 1000
 * \f]
 *
 * The 2 comes from the min token size (1), plus a whitespace char as token
 * delimiter.
 */
#define MAX_TOKEN_NUM 1000
#define MAX_CONCURRENT_JOBS 20	//! Max number of concurrent jobs as per requirements
#define CHILD_COUNT_SIMPLE 1	//! Number of children processes in a simple command without pipes
#define CHILD_COUNT_PIPE 2		//! Number of children processes in a command with a pipe
#define SYSCALL_RETURN_ERR -1	//! Value returned on a system call error
#define BUFF_SIZE_TIMESTAMP 24	//! Timestamp string buffer size

#define EMPTY_STR "\0"
#define EMPTY_ARRAY -1

#define DAEMON_PORT 3826					//! Default daemon TCP server port
#define DAEMON_DIR "/tmp/"					//! Daemon safe directory
#define DAEMON_LOG_PATH "/tmp/yashd.log"	//! Daemon log path
#define DAEMON_PID_PATH "/tmp/yashd.pid"	//! Daemon PID file path
#define DAEMON_UMASK 0						//! Daemon umask

#define TCP_PORT_LOWER_LIM 1024		//! Lowest TCP port allowed
#define TCP_PORT_HIGHER_LIM 65535	//! Highest TCP port allowed

#define CMD_BG "bg\0"		//! Shell command bg, @sa bg()
#define CMD_FG "fg\0"		//! Shell command fg, @sa fg()
#define CMD_JOBS "jobs\0"	//! Shell command jobs, @sa jobs()

#define JOB_STATUS_RUNNING "Running\0"	//! Shell job status running
#define JOB_STATUS_STOPPED "Stopped\0"	//! Shell job status stopped
#define JOB_STATUS_DONE "Done\0"		//! Shell job status done

#define EXIT_OK 0			//! No error
#define EXIT_ERR 1			//! Unknown error
#define EXIT_ERR_ARG 2		//! Wrong argument provided
#define EXIT_ERR_DAEMON 3	//! Daemon process error
#define EXIT_ERR_SOCKET 4	//! Socket error
#define EXIT_ERR_CMD 5		//! Command syntax error


/**
 * @brief Struct to organize all the command line arguments.
 *
 * Arguments:
 *   - verbose: enable debugging log output
 *   - port: port of the TCP server
 */
typedef struct _cmd_args_t {
	bool verbose;	// Logger verbose output
	int port;		// Server port
} cmd_args_t;


/**
 * @brief Struct to organize all the necessary thread arguments
 */
typedef struct _thread_args_t {
	cmd_args_t cmd_args;	// Command line arguments
} thread_args_t;


/**
 * @brief Struct to organize all information of a shell command.
 *
 * The raw input string should be saved to `cmd_str`. The tokenized command
 * should be saved to `cmd_tok`, and the size of that array to `cmd_tok_size`.
 *
 * The requirements only require to have a single pipe, which allows for a
 * maximum of 2 commands. The command to the left of the pipe symbol (or if
 * there is no pipe) should be saved as a tokenized array in `cmd1`. The command
 * to the right of the pipe should be saved to `cmd2`. If there is a pipe,
 * `pipe` should be `1`. Else, it should be `0`.
 *
 * If there is redirection, the path to the redirection files is saved on the
 * `in1`, `in2`, `out1`, `out2`, `err1` and `err2` attributes. If any of those
 * struct members is `"\0"`, they are assumed to use their default files stdin,
 * stdout or stderr.
  *
 * If the command is to be run in the background, `bg` should be set to `1`, or
 * `0` for foreground.
 *
 * If there is an error parsing or setting any part of the command, `err_msg`
 * must be set to the error message string. Else, `err_msg` must be set to
 * `"\0"`.
 */
typedef struct Jobs {
	char cmd_str[MAX_CMD_LEN+1];		// Input command as a string
	char* cmd_tok[MAX_TOKEN_NUM];		// Tokenized input command
	uint32_t cmd_tok_len;				// Number of tokens in command
	char* cmd1[MAX_TOKEN_NUM];			// Command and arguments to execute
	char in1[MAX_TOKEN_LEN+1];			// Cmd1 input redirection
	char out1[MAX_TOKEN_LEN+1];			// Cmd1 output redirection
	char err1[MAX_TOKEN_LEN+1];			// Cmd1 error redirection
	char* cmd2[MAX_TOKEN_NUM];			// Second command if there is a pipe
	char in2[MAX_TOKEN_LEN+1];			// Cmd2 input redirection
	char out2[MAX_TOKEN_LEN+1];			// Cmd2 output redirection
	char err2[MAX_TOKEN_LEN+1];			// Cmd2 error redirection
	bool pipe;							// Pipe boolean
	bool bg;							// Background process boolean
	pid_t gpid;							// Group PID
	uint8_t jobno;						// Job number
	char status[MAX_STATUS_LEN];		// Status of the process group
	char err_msg[MAX_ERROR_LEN];		// Error message
} Job;


// Functions
bool ignoreInput(char* input_str);
void removeJob(int job_idx);
void printJob(int job_idx);
void bgExec();
void fgExec();
void jobsExec();
bool runShellComd(char* input);
void tokenizeString(Job* cmd_tok);
void parseJob(char* cmd_str, Job jobs_arr[], int* last_job);
void redirectSimple(Job* cmd);
void redirectPipe(Job* cmd);
void waitForChildren(Job* cmd);
void runJob(Job jobs_arr[], int* last_job);
void handleNewJob(char* input);
void maintainJobsTable();
void killAllJobs();
void *run_shell(void *arg);

char *timeStr(char *buff, int size);
bool isNumber(char number[]);
cmd_args_t parseArgs(int argc, char** argv);
void sigPipe(int n);
void sigChld(int n);
void daemonInit(const char *const path, uint mask);
void reusePort(int sock);
int createSocket(int port);

int main(int argc, char** argv);

#endif

