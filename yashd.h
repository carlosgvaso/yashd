/**
 * @file  yashd.h
 *
 * @brief Yash shell daemon
 *
 * @author:	Jose Carlos Martinez Garcia-Vaso <carlosgvaso@utexas.edu>
 * @author: Utkarsh Vardan <uvardan@utexas.edu>
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
#include <pthread.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "yashd_defs.h"

#define PATHMAX 255			//! Max length of a path
#define MAX_CONCURRENT_CLIENTS 50	//! Max number of clients connected
#define MAX_CONNECT_QUEUE 5	//! Max queue of pending connections
#define MAIN_LOOP_SLEEP_TIME 0.5	//! Main loop time to sleep between iters
#define MAX_STATUS_LEN 8	//! Max status string length
/**
 * @brief Max number of tokens per command.
 *
 * This is calculated as follows:
 *
 * \f[
 * \frac{MAX_CMD_LEN}{2} = 1000
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

//#define DAEMON_PORT 3826					//! Default daemon TCP server port
#define DAEMON_DIR "/tmp/"					//! Daemon safe directory
#define DAEMON_LOG_PATH "/tmp/yashd.log"	//! Daemon log path
#define DAEMON_PID_PATH "/tmp/yashd.pid"	//! Daemon PID file path
#define DAEMON_UMASK 0						//! Daemon umask

#define MSG_TYPE_CTL "CTL\0"	//! Control message token
#define MSG_TYPE_CMD "CMD\0"	//! Command message token
#define MSG_CTL_SIGINT 'c'		//! Control message argument for SIGINT (ctrl+c)
#define MSG_CTL_SIGTSTP 'z'		//! Control message argument for SIGTSTP (ctrl+z)
#define MSG_CTL_EOF 'd'			//! Control message argument for EOF (ctrl+d)
#define MSG_TYPE_DELIM " "		//! Type (1st word) token delimiter
#define MSG_ARGS_DELIM "\0"		//! Arguments token delimiter

#define CMD_PROMPT "\n# \0"	//! Shell prompt
#define CMD_BG "bg\0"		//! Shell command bg, @sa bg()
#define CMD_FG "fg\0"		//! Shell command fg, @sa fg()
#define CMD_JOBS "jobs\0"	//! Shell command jobs, @sa jobs()

#define JOB_STATUS_RUNNING "Running\0"	//! Shell job status running
#define JOB_STATUS_STOPPED "Stopped\0"	//! Shell job status stopped
#define JOB_STATUS_DONE "Done\0"		//! Shell job status done


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
 * @brief Struct to organize all the necessary arguments passed to a thread
 */
typedef struct _th_args_t {
	cmd_args_t cmd_args;		// Command line arguments
	int idx;					// Thread table index
	int ps;						// Socket fd
	struct sockaddr_in from;	// Client connection information
} th_args_t;


/**
 * @brief Struct to organize all the info for an entry in the threads table
 */
typedef struct _th_info {
	pthread_t tid;
	bool run;
	int socket;
	int pid;
	int pthread_pipe_fd[2];
} th_info_t;


/**
 * @brief Struct to organize the received messages from the client
 */
typedef struct _msg_args {
	char type[4];				// CMD/CTL
	char args[MAX_CMD_LEN+1];	// Message arguments
} msg_args_t;


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
typedef struct _job_info {
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
} job_info_t;

/**
 * \brief Information necessary for the shell to run jobs
 */
typedef struct _shell_info {
	th_args_t *th_args;							// Thread arguments pointer
	job_info_t jobs_table[MAX_CONCURRENT_JOBS];	// Jobs table
	int jobs_table_idx;							// Number of jobs in table
} shell_info_t;


// Functions
bool ignoreInput(char* input_str);
void removeJob(int job_idx, shell_info_t *shell_info);
void printJob(int job_idx, shell_info_t *shell_info);
void bgExec();
void fgExec();
void jobsExec(shell_info_t *shell_info);
bool runShellComd(char* input, shell_info_t *shell_info);
void tokenizeString(job_info_t* cmd_tok);
void parseJob(char* cmd_str, shell_info_t *shell_info);
void redirectSimple(job_info_t* cmd);
void redirectPipe(job_info_t* cmd);
void waitForChildren(job_info_t* cmd, shell_info_t *shell_info);
void runJob(shell_info_t *shell_info);
void handleNewJob(char* input, shell_info_t *shell_info);
void maintainJobsTable(shell_info_t *shell_info);
void killAllJobs(shell_info_t *shell_info);
int startJob(char *job_str, shell_info_t *shell_info);

char *timeStr(char *buff, int size);
bool isNumber(char number[]);
cmd_args_t parseArgs(int argc, char** argv);
void sigPipe(int n);
void sigChld(int n);
void daemonInit(const char *const path, uint mask);
void reusePort(int sock);
int createSocket(int port);
void printThTable();
int searchThByTid(pthread_t tid);
void removeThFromTableByIdx(int idx);
void removeThFromTableByTid(pthread_t tid);
void stopAllThreads();
void exitThreadSafely();
msg_args_t parseMessage(char *msg);
void handleCTLMessages(char arg, th_args_t *thread_args,
		shell_info_t *shell_info);
void handleCMDMessages(char *args, th_args_t *thread_args,
		shell_info_t *shell_info);
void *serverThread(void *args);
int main(int argc, char** argv);

#endif

