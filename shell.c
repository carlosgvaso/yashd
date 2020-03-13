/**
 * @file  shell.c
 *
 * @brief Main functionality of the yash shell
 *
 * @author:	Jose Carlos Martinez Garcia-Vaso <carlosgvaso@utexas.edu>
 * @author: Utkarsh Vardan <uvardan@utexas.edu>
 */

#include "yashd.h"


// Globals
extern int errno;


/**
 * \brief Check for input that should be ignored.
 *
 * This function checks for empty input strings and strings consisting of
 * whitespace characters only, and ignores such input.
 *
 * \param	input_str	Inupt string to check
 * \return	1 if the input should be ignored, 0 otherwise
 */
bool ignoreInput(char* input_str) {
	// Check for empty command string
	if (!strcmp(input_str, EMPTY_STR)) {
		return (true);
	}

	// Check for a command string containing only whitespace
	uint8_t whitespace = true;
	for (size_t i=0; i<strlen(input_str); i++) {
		if (!isspace(input_str[i])) {
			whitespace = false;
		}
	}

	if (whitespace) {
		return (true);
	}

	return (false);
}


/**
 * \brief Remove job from jobs table
 *
 * \param	job_idx		Job index in the job_arr
 * \param	shell_info	Shell info struct pointer
 */
void removeJob(int job_idx, shell_info_t *shell_info) {
	// Clear job entries
	shell_info->jobs_table[job_idx].jobno = 0;
	shell_info->jobs_table[job_idx].gpid = 0;
	strcpy(shell_info->jobs_table[job_idx].status, "\0");

	// Iterate over the table backwards to lower table index
	for (int i=(shell_info->jobs_table_idx)-1; i>=0; i--) {
		// Reduce the index if thread at the end of the table is done
		if (shell_info->jobs_table[shell_info->jobs_table_idx].jobno < 1) {
			(shell_info->jobs_table_idx)--;
		} else {	// Exit when we find the last running thread
			break;
		}
	}
}


/**
 * \brief Print job information
 *
 * TODO: Send output to client
 *
 * \param	job_idx		Job array index
 * \param	shell_info	Shell info struct pointer
 */
void printJob(int job_idx, shell_info_t *shell_info) {
	char buf[MAX_CMD_LEN+1];

	// Print the job number
	//fprintf(stderr, "[%d]", jobs_table[job_idx].jobno);
	sprintf(buf, "[%d]", shell_info->jobs_table[job_idx].jobno);
	send(shell_info->th_args->ps, buf, (size_t) strlen(buf), 0);

	// Print current job indicator
	if ((shell_info->jobs_table_idx)-1 == job_idx) {
		//fprintf(stderr, "+");
		sprintf(buf, "+");
		send(shell_info->th_args->ps, buf, (size_t) strlen(buf), 0);
	} else {
		//fprintf(stderr, "-");
		sprintf(buf, "-");
		send(shell_info->th_args->ps, buf, (size_t) strlen(buf), 0);
	}

	// Print job status
	//fprintf(stderr, " %s", shell_info->jobs_table[job_idx].status);
	sprintf(buf, " %s\t", shell_info->jobs_table[job_idx].status);
	send(shell_info->th_args->ps, buf, (size_t) strlen(buf), 0);

	// Print job command string
	//fprintf(stderr, "\t");
	for (int j=0; j<shell_info->jobs_table[job_idx].cmd_tok_len; j++) {
		//fprintf(stderr, "%s ", shell_info->jobs_table[job_idx].cmd_tok[j]);
		sprintf(buf, "%s ", shell_info->jobs_table[job_idx].cmd_tok[j]);
		send(shell_info->th_args->ps, buf, (size_t) strlen(buf), 0);
	}
	//fprintf(stderr, "\n");
}


/**
 * @brief Send command to the background.
 *
 * TODO: Implement bg
 */
void bgExec() {

}


/**
 * @brief Send command to the foreground.
 *
 * TODO: Implement fg
 */
void fgExec() {

}


/**
 * \brief Display jobs table.
 *
 * \param	shell_info	Shell info struct pointer
 */
void jobsExec(shell_info_t *shell_info) {
	const char JOBS_MSG1[MAX_ERROR_LEN] = "No jobs in job table\n";

	// Update the jobs table
	maintainJobsTable(shell_info);

	// Check we at least have one job in the list
	if (shell_info->jobs_table_idx <= 0) {
		// TODO: Send this output to the client
		//fprintf(stderr, "No jobs in job table\n");
		send(shell_info->th_args->ps, JOBS_MSG1, (size_t) strlen(JOBS_MSG1), 0);
		return;
	}

	// Iterate over all the jobs in the array
	for (int i=0; i<shell_info->jobs_table_idx; i++) {
		// Only print active jobs
		if (!strcmp(shell_info->jobs_table[i].status, JOB_STATUS_RUNNING) ||
				!strcmp(shell_info->jobs_table[i].status, JOB_STATUS_STOPPED)) {
			// Print the job info
			printJob(i, shell_info);
		}
	}
}


/**
 * \brief Check if input is shell command, and run it.
 *
 * \param	input		Raw input string
 * \param	shell_info	Shell info struct pointer
 * \return	True if shell command ran, false if it is not a shell command
 */
bool runShellCmd(char* input, shell_info_t *shell_info) {
	if (!strcmp(input, CMD_BG)) {
		bgExec();
		return true;
	} else if (!strcmp(input, CMD_FG)) {
		fgExec();
		return true;
	} else if (!strcmp(input, CMD_JOBS)) {
		jobsExec(shell_info);
		return true;
	}
	return false;
}


/**
 * \brief Split a command string into string tokens.
 *
 * This function assumes the `cmd.cmd_str` is not an empty string.
 *
 * This function uses strtok() to split a command string into string tokens.
 * Tokens are considered to be contiguous characters separated by whitespace.
 *
 * \param	cmd	Command struct
 *
 * \sa strtok(), Cmd
 */
void tokenizeString(job_info_t* cmd) {
	const char CMD_TOKEN_DELIM = ' ';	// From requirements
	size_t len = 0;

	// Remove final newline char and replace with NULL char
	len = strlen(cmd->cmd_str);
	if (cmd->cmd_str[len-1] == '\n') {
		cmd->cmd_str[len-1] = '\0';
	}

	// Break down the command into tokens using strtok
	cmd->cmd_tok[0] = strtok(cmd->cmd_str, &CMD_TOKEN_DELIM);
	uint32_t count = 1;	// Start at one because we already run strtok once

	while ((cmd->cmd_tok[count] = strtok(NULL, &CMD_TOKEN_DELIM))) {
		count++;
	}
	cmd->cmd_tok_len = count;
}


/**
 * \brief Parse a command.
 *
 * This function assumes the `cmd.cmd_str` is not an empty string.
 *
 * This function takes a raw command string, and parses it to load it into a
 * `Job` struct as per the requirements.
 *
 * TODO: Might need check for multiple redirections of the same type to raise an error.
 *
 * \param	cmd_str		Raw command string
 * \param	shell_info	Shell info struct pointer
 *
 * @sa	Cmd
 */
void parseJob(char* cmd_str, shell_info_t *shell_info) {
	const char I_REDIR_OPT[2] = "<\0";
	const char O_REDIR_OPT[2] = ">\0";
	const char E_REDIR_OPT[3] = "2>\0";
	const char BG_OPT[2] = "&\0";
	const char PIPE_OPT[2] = "|\0";
	const char SYNTAX_ERR_1[MAX_ERROR_LEN] = "syntax error: command should not"
			" start with \0";
	const char SYNTAX_ERR_2[MAX_ERROR_LEN] = "syntax error: near token \0";
	const char SYNTAX_ERR_3[MAX_ERROR_LEN] = "syntax error: command should not"
			" end with \0";
	const char SYNTAX_ERR_4[MAX_ERROR_LEN] = "syntax error: & should be the last"
			" token of the command\0";

	// Save and tokenize command string
	strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_str,
			cmd_str);
	tokenizeString(&(shell_info->jobs_table[(shell_info->jobs_table_idx)-1]));

	/*
	 * Iterate over all tokens to look for arguments, redirection directives,
	 * pipes and background directives
	 */
	int cmd_count = 0;	// Command array counter
	for (uint32_t i=0; i<shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok_len; i++) {
		if (!strcmp(I_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i])) {	// Check for input redir
			// Check if redirection token has the correct syntax
			if (i <= 0 || cmd_count <= 0) {	// Check it is not the first token
				strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, SYNTAX_ERR_1);
				strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				return;
			} else if (i >= shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok_len-1) {	// Check it is not the last token
				strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, SYNTAX_ERR_3);
				strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				return;
			} else if (!strcmp(I_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(O_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(E_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(PIPE_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(BG_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1])) {	// Check there is an argument after this token
				strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, SYNTAX_ERR_2);
				strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				return;
			} else {	// Correct syntax
				i++;	// Move ahead one iter to get the redir argument
				if (!shell_info->jobs_table[(shell_info->jobs_table_idx)-1].pipe) {
					strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].in1, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				} else {
					strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].in2, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				}
			}
		} else if (!strcmp(O_REDIR_OPT,shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i])) {	// Output redir
			// Check if redirection token has the correct syntax
			if (i <= 0 || cmd_count <= 0) {	// Check it is not the first token
				strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, SYNTAX_ERR_1);
				strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				return;
			} else if (i >= shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok_len-1) {	// Check it is not the last token
				strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, SYNTAX_ERR_3);
				strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				return;
			} else if (!strcmp(I_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(O_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(E_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(PIPE_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(BG_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1])) {	// Check there is an argument after this token
				strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, SYNTAX_ERR_2);
				strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				return;
			} else {	// Correct syntax
				i++;	// Move ahead one iter to get the redir argument
				if (!shell_info->jobs_table[(shell_info->jobs_table_idx)-1].pipe) {
					strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].out1, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				} else {
					strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].out2, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				}
			}
		} else if (!strcmp(E_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i])) {	// Error redir
			// Check if redirection token has the correct syntax
			if (i <= 0 || cmd_count <= 0) {	// Check it is not the first token
				strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, SYNTAX_ERR_1);
				strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				return;
			} else if (i >= shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok_len-1) {	// Check it is not the last token
				strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, SYNTAX_ERR_3);
				strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				return;
			} else if (!strcmp(I_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(O_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(E_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(PIPE_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(BG_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1])) {	// Check there is an argument after this token
				strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, SYNTAX_ERR_2);
				strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				return;
			} else {	// Correct syntax
				i++;	// Move ahead one iter to get the redir argument
				if (!shell_info->jobs_table[(shell_info->jobs_table_idx)-1].pipe) {
					strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err1, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				} else {
					strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err2, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				}
			}
		} else if (!strcmp(PIPE_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i])) {	// Pipe command
			// Check if pipe token has the correct syntax
			if (i <= 0 || cmd_count <= 0) {	// Check it is not the first token
				strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, SYNTAX_ERR_1);
				strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				return;
			} else if (i >= shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok_len-1) {	// Check it is not the last token
				strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, SYNTAX_ERR_3);
				strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				return;
			} else if (!strcmp(I_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(O_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(E_REDIR_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(PIPE_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1]) ||
					!strcmp(BG_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i+1])) {	// Check there is an argument after this token
				strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, SYNTAX_ERR_2);
				strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i]);
				return;
			} else {	// Correct syntax
				shell_info->jobs_table[(shell_info->jobs_table_idx)-1].pipe = true;
				cmd_count = 0;	// Start argument count for cmd2
			}
		} else if (!strcmp(BG_OPT, shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i])) {	// Background command
			// Check if background token has the correct syntax
			if (i != shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok_len-1) {	// Check if it is not the last token
				strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, SYNTAX_ERR_4);
				return;
			} else {
				shell_info->jobs_table[(shell_info->jobs_table_idx)-1].bg = true;
			}
		} else {	// Command argument
			if (!shell_info->jobs_table[(shell_info->jobs_table_idx)-1].pipe) {
				shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd1[cmd_count] = shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i];
				cmd_count++;
			} else {
				shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd2[cmd_count] = shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd_tok[i];
				cmd_count++;
			}
		}
	}
}


/**
 * \brief Redirect input, output and error of simple commands without pipes, or
 * left child of a pipe.
 *
 * \param	cmd	Command to set the redirection
 */
void redirectSimple(job_info_t* cmd) {
	const char REDIR_ERR_1[MAX_ERROR_LEN] = "open errno ";
	const char REDIR_ERR_2[MAX_ERROR_LEN] = ": could not open file: ";
	extern errno;
	char errno_str[sizeof(int)*8+1];

	// Input redirection
	if (strcmp(cmd->in1, EMPTY_STR)) {
		//
		int i1fd = open(cmd->in1, O_RDONLY,
						S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

		if (i1fd == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, REDIR_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, REDIR_ERR_2);
			strcat(cmd->err_msg, cmd->in1);
			return;
		}

		dup2(i1fd, STDIN_FILENO);
		close(i1fd);
	}

	// Output redirection
	if (strcmp(cmd->out1, EMPTY_STR)) {
		// Create output file
		int o1fd = open(cmd->out1, O_WRONLY|O_CREAT|O_TRUNC,
				S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

		if (o1fd == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, REDIR_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, REDIR_ERR_2);
			strcat(cmd->err_msg, cmd->out1);
			return;
		}

		dup2(o1fd, STDOUT_FILENO);
		close(o1fd);
	}

	// Error redirection
	if (strcmp(cmd->err1, EMPTY_STR)) {
		//
		int e1fd = open(cmd->err1, O_WRONLY|O_CREAT|O_TRUNC,
						S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

		if (e1fd == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, REDIR_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, REDIR_ERR_2);
			strcat(cmd->err_msg, cmd->err1);
			return;
		}

		dup2(e1fd, STDERR_FILENO);
		close(e1fd);
	}
}


/**
 * \brief Redirect input, output and error of right child of a pipe.
 *
 * \param	cmd	Command to set the redirection
 */
void redirectPipe(job_info_t* cmd) {
	const char REDIR_ERR_1[MAX_ERROR_LEN] = "open errno ";
	const char REDIR_ERR_2[MAX_ERROR_LEN] = ": could not open file: ";
	extern errno;
	char errno_str[sizeof(int)*8+1];

	// Input redirection
	if (cmd->pipe && strcmp(cmd->in2, EMPTY_STR)) {
		//
		int i2fd = open(cmd->in2, O_RDONLY,
						S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

		if (i2fd == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, REDIR_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, REDIR_ERR_2);
			strcat(cmd->err_msg, cmd->in2);
			return;
		}

		dup2(i2fd, STDIN_FILENO);
		close(i2fd);
	}

	// Output redirection
	if (cmd->pipe && strcmp(cmd->out2, EMPTY_STR)) {
		//
		int o2fd = open(cmd->out2, O_WRONLY|O_CREAT|O_TRUNC,
						S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

		if (o2fd == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, REDIR_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, REDIR_ERR_2);
			strcat(cmd->err_msg, cmd->out2);
			return;
		}

		dup2(o2fd, STDOUT_FILENO);
		close(o2fd);
	}

	// Error redirection
	if (cmd->pipe && strcmp(cmd->err2, EMPTY_STR)) {
		//
		int e2fd = open(cmd->err2, O_WRONLY|O_CREAT|O_TRUNC,
						S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);

		if (e2fd == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, REDIR_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, REDIR_ERR_2);
			strcat(cmd->err_msg, cmd->err2);
			return;
		}

		dup2(e2fd, STDERR_FILENO);
		close(e2fd);
	}

}


/**
 * \brief Set up signal handling to relay signals to children processes.
 *
 * This function is based on the UT Austin EE 382V Systems Programming class
 * examples posted by Dr. Ramesh Yerraballi.
 *
 * TODO: Add support for job control.
 *
 * \param	cmd			Parsed command
 * \param	shell_info	Shell info struct pointer
 */
void waitForChildren(job_info_t* cmd, shell_info_t *shell_info) {
	const char SIG_ERR_1[MAX_ERROR_LEN] = "signal errno ";
	const char SIG_ERR_2[MAX_ERROR_LEN] = ": waitpid error";
	extern errno;
	char errno_str[sizeof(int)*8+1];

	int status;
	uint8_t count = 0;
	int child_num;

	// Determine the number of child processes
	if (cmd->pipe) {
		child_num = CHILD_COUNT_PIPE;
	} else {
		child_num = CHILD_COUNT_SIMPLE;
	}

	// Wait for child to exit
	while (count < child_num) {
		/**
		 * TODO: Fix EINTR (4) error
		 *
		 * This snippet always throws the EINTR (4) error code. The
		 * documentation for waitpid (3) says EINTR means: "WNOHANG was not set
		 * and an unblocked signal or a SIGCHLD was caught; see signal(7)."
		 *
		 * TODO: Fix WCONTINUED compilation error
		 *
		 * See this for error description: https://stackoverflow.com/questions/
		 * 60101242/compiler-error-using-wcontinued-option-for-waitpid
		 */
		//if (waitpid(-1, &status, WUNTRACED|WCONTINUED) == SYSCALL_RETURN_ERR) {
		if (waitpid(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].gpid, &status, WUNTRACED) == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(cmd->err_msg, SIG_ERR_1);
			strcat(cmd->err_msg, errno_str);
			strcat(cmd->err_msg, SIG_ERR_2);
			return;
		}

		if (WIFEXITED(status)) {
			/*
			if (args->cmd_args.verbose) {
				printf("-yash: child process terminated normally\n");
			}
			*/
			count++;
		} else if (WIFSIGNALED(status)) {
			/*
			printf("\n");	// Ensure there is an space after "^C"
			if (args->cmd_args.verbose) {
				printf("-yash: child process terminated by a signal\n");
			}
			*/
			count++;
		} else if (WIFSTOPPED(status)) {
			/*
			printf("\n");	// Ensure there is an space after "^Z"
			if (args->cmd_args.verbose) {
				printf("-yash: child process stopped by a signal\n");
			}
			*/
			//
		} /*else if (WIFCONTINUED(status)) {
			//
		}*/
	}
}


/**
 * \brief Execute commands.
 *
 * This function allows for both simple and piped commands (1 pipe only).
 * Moreover, the command is checked for correctness, and ignored if it does not
 * exist.
 *
 * This function is based on the UT Austin EE 382V Systems Programming class
 * examples by Dr. Ramesh Yerraballi.
 *
 * TODO: Add support for job control.
 *
 * \param	shell_info	Shell info struct pointer
 */
void runJob(shell_info_t *shell_info) {
	char buf[MAX_CMD_LEN+1];
	const char PIPE_ERR_1[MAX_ERROR_LEN] = "pipe errno ";
	const char PIPE_ERR_2[MAX_ERROR_LEN] = ": failed to make pipe";
	extern errno;
	char errno_str[sizeof(int)*8+1];

	pid_t c1_pid, c2_pid;
	int pfd[2];
	//int stdout_fd;	// Not needed since stdin/out will be the socket



	if (shell_info->jobs_table[(shell_info->jobs_table_idx)-1].pipe) {
		//stdout_fd = dup(STDOUT_FILENO);	// Save stdout

		if (pipe(pfd) == SYSCALL_RETURN_ERR) {
			sprintf(errno_str, "%d", errno);
			strcpy(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, PIPE_ERR_1);
			strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, errno_str);
			strcat(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, PIPE_ERR_2);
			return;
		}
	}

	c1_pid = fork();

	if (c1_pid == 0) {	// Child 1 or left child process
		// Create a new session and a new group, and become group leader
		setpgid(0, 0);

		// Set up signal handling sent to the children process group
		if (shell_info->th_args->cmd_args.verbose) {
			/*
			printf("-yash: children process group: ignoring signal SIGTTOU, "
					"but getting all the others\n");
			*/
			sprintf(buf, "-yash: children process group: ignoring signal "
					"SIGTTOU, but getting all the others\n");
			send(shell_info->th_args->ps, buf, (size_t) strlen(buf), 0);
		}
		signal(SIGTTOU, SIG_IGN);
		signal(SIGINT, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
		signal(SIGCHLD, SIG_DFL);

		// Setup the socket as stdin/out, and make necessary changes for pipes
		if (shell_info->jobs_table[(shell_info->jobs_table_idx)-1].pipe) {
			close(pfd[0]);	// Close unused read end
			dup2(pfd[1], STDOUT_FILENO);	// Make output go to pipe
			dup2(shell_info->th_args->ps, STDERR_FILENO);	// Send the stderr to socket
			close(shell_info->th_args->ps);
		} else {
			dup2(shell_info->th_args->ps, STDOUT_FILENO);	// Send the output to socket
			dup2(shell_info->th_args->ps, STDERR_FILENO);	// Send the stderr to socket
			close(shell_info->th_args->ps);
		}

		// Do additional redirection if necessary
		redirectSimple(&(shell_info->jobs_table[(shell_info->jobs_table_idx)-1]));
		if (strcmp(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, EMPTY_STR)) {
			/*
			if (shell_info->jobs_table[(shell_info->jobs_table_idx)-1].pipe) {
				dup2(stdout_fd, STDOUT_FILENO);	// Allow to write to stdout
			}
			*/
			printf("-yash: %s\n", shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg);
			//sprintf(buf, "-yash: %s\n",
			//		shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg);
			send(shell_info->th_args->ps, buf, (size_t) strlen(buf), 0);
			exit(EXIT_ERR_CMD);
		}

		// Execute command
		if (execvp(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd1[0], shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd1) == SYSCALL_RETURN_ERR
				&& shell_info->th_args->cmd_args.verbose) {
			printf("-yash: execvp() errno: %d\n", errno);
			//sprintf(buf, "-yash: execvp() errno: %d\n", errno);
			//send(shell_info->th_args->ps, buf, (size_t) strlen(buf), 0);
		}
		// Make sure we terminate child on execvp() error
		exit(EXIT_ERR_CMD);
	} else {	// Parent process
		if (shell_info->jobs_table[(shell_info->jobs_table_idx)-1].pipe) {
			c2_pid = fork();

			if (c2_pid == 0) {	// Child 2 or right child process
				// Join the group created by child 1
				setpgid(0, c1_pid);

				// Set up signal handling sent to the children process group
				/*
				if (args->cmd_args.verbose) {
					printf("-yash: children process group: ignoring signal SIGTTOU, "
							"but getting all the others\n");
				}
				*/
				signal(SIGTTOU, SIG_IGN);
				signal(SIGINT, SIG_DFL);
				signal(SIGTSTP, SIG_DFL);
				signal(SIGCHLD, SIG_DFL);

				close(pfd[1]);	// Close unused write end
				dup2(pfd[0], STDIN_FILENO);	// Get input from pipe
				dup2(shell_info->th_args->ps, STDOUT_FILENO);	// Send the output to socket
				dup2(shell_info->th_args->ps, STDERR_FILENO);	// Send the stderr to socket
				close(shell_info->th_args->ps);

				// Do additional redirection if necessary
				redirectPipe(&(shell_info->jobs_table[(shell_info->jobs_table_idx)-1]));
				if (strcmp(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, EMPTY_STR)) {
					//dup2(stdout_fd, STDOUT_FILENO);	// Allow to write to stdout
					printf("-yash: %s\n", shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg);
					exit(EXIT_ERR_CMD);
				}

				// Execute command
				if (execvp(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd2[0], shell_info->jobs_table[(shell_info->jobs_table_idx)-1].cmd2) == SYSCALL_RETURN_ERR
						&& shell_info->th_args->cmd_args.verbose) {
					printf("-yash: execvp() errno: %d\n", errno);
				}
				// Make sure we terminate child on execvp() error
				exit(EXIT_ERR_CMD);
			}
			// Parent process. Close pipes so EOF can work
			close(pfd[0]);
			close(pfd[1]);
			//close(stdout_fd);
		}

		// Parent process
		// Save job gpid
		shell_info->jobs_table[(shell_info->jobs_table_idx)-1].gpid = c1_pid;
		if (!shell_info->jobs_table[(shell_info->jobs_table_idx)-1].bg) {
			// Give terminal control to child
			/*
			if (args->cmd_args.verbose) {
				printf("-yash: "
						"giving terminal control to child process group\n");
			}
			*/
			tcsetpgrp(0, c1_pid);

			// Block while waiting for children
			waitForChildren(&(shell_info->jobs_table[(shell_info->jobs_table_idx)-1]), shell_info);
			if (strcmp(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, EMPTY_STR)) {
				return;
			}

			// Get back terminal control to parent
			/*
			if (args->cmd_args.verbose) {
				printf("-yash: returning terminal control to parent process\n");
			}
			*/
			tcsetpgrp(0, getpid());
			removeJob((shell_info->jobs_table_idx)-1, shell_info);	// Remove job from jobs table
		}
	}
}


/**
 * \brief Handle new job.
 *
 * This function parses the raw input of the new job, adds the job to the jobs
 * table, and it executes the new job.
 *
 * \param	input		Raw input of the new job
 * \param	shell_info	Shell info struct pointer
 */
void handleNewJob(char* input, shell_info_t *shell_info) {
	char buf[MAX_ERROR_LEN+10];
	// Initialize a new Job struct
	job_info_t job = {
		EMPTY_STR,		// cmd_str
		{ EMPTY_STR },	// cmd_tok
		0,				// cmd_tok_size
		{ EMPTY_STR },	// cmd1
		EMPTY_STR,		// in1
		EMPTY_STR,		// out1
		EMPTY_STR,		// err1
		{ EMPTY_STR },	// cmd2
		EMPTY_STR,		// in2
		EMPTY_STR,		// out2
		EMPTY_STR,		// err2
		false,			// pipe
		false,			// bg
		EMPTY_ARRAY,	// gpid
		EMPTY_ARRAY,	// jobno
		EMPTY_STR,		// status
		EMPTY_STR		// err_msg
	};

	// Add command to the jobs array
	if ((shell_info->jobs_table_idx)-1 < MAX_CONCURRENT_JOBS) {
		shell_info->jobs_table[shell_info->jobs_table_idx] = job;
		shell_info->jobs_table[shell_info->jobs_table_idx].jobno =
				(shell_info->jobs_table_idx)+1;
		strcpy(shell_info->jobs_table[shell_info->jobs_table_idx].status,
				JOB_STATUS_RUNNING);
		(shell_info->jobs_table_idx)++;
	} else {
		// Send output to client
		/*
		printf("-yash: max number of concurrent jobs reached: %d",
				shell_info->jobs_table_idx);
		*/
		sprintf(buf, "-yash: max number of concurrent jobs reached: %d",
				MAX_CONCURRENT_JOBS);
		send(shell_info->th_args->ps, buf, (size_t) strlen(buf), 0);
		return;
	}

	// Parse job
	if (shell_info->th_args->cmd_args.verbose) {
		//printf("-yash: parsing input...\n");
		sprintf(buf, "-yash: parsing input...\n");
		send(shell_info->th_args->ps, buf, (size_t) strlen(buf), 0);
	}
	parseJob(input, shell_info);
	if (strcmp(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg,
			EMPTY_STR)) {
		//printf("-yash: %s\n", jobs_table[last_job].err_msg);
		sprintf(buf, "-yash: %s\n",
				shell_info->jobs_table[shell_info->jobs_table_idx].err_msg);
		send(shell_info->th_args->ps, buf, (size_t) strlen(buf), 0);
		return;
	}

	// Run job
	if (shell_info->th_args->cmd_args.verbose) {
		//printf("-yash: executing command...\n");
		sprintf(buf, "-yash: executing command...\n");
		send(shell_info->th_args->ps, buf, (size_t) strlen(buf), 0);
	}

	runJob(shell_info);
	if (shell_info->jobs_table_idx != EMPTY_ARRAY) {
		if (strcmp(shell_info->jobs_table[(shell_info->jobs_table_idx)-1].err_msg, EMPTY_STR)) {
			//printf("-yash: %s\n", shell_info->jobs_table[shell_info->jobs_table_idx].err_msg);
			sprintf(buf, "-yash: %s\n",
					shell_info->jobs_table[shell_info->jobs_table_idx].err_msg);
			send(shell_info->th_args->ps, buf, (size_t) strlen(buf), 0);
			return;
		}
	}
}


/**
 * \brief Check if any background jobs finished.
 *
 * Check if any previously running job in the jobs table has finished running.
 *
 * \param	shell_info	Shell info struct pointer
 */
void maintainJobsTable(shell_info_t *shell_info) {
	// Check every job in the jobs_table
	for (int i=0; i<shell_info->jobs_table_idx; i++) {
		// Skip jobs that already finished
		if (!strcmp(shell_info->jobs_table[i].status, JOB_STATUS_RUNNING) ||
				!strcmp(shell_info->jobs_table[i].status, JOB_STATUS_STOPPED)) {
			int status;
			if (waitpid(shell_info->jobs_table[i].gpid, &status, WNOHANG|WUNTRACED|WCONTINUED) == SYSCALL_RETURN_ERR) {
				perror("Error checking child status");
				// TODO: handle error
			}
			if (WIFEXITED(status)) {
				// Change status to done and, remove child from array
				strcpy(shell_info->jobs_table[i].status, JOB_STATUS_DONE);
				// TODO: Send output to client
				printJob(i, shell_info);
				removeJob(i, shell_info);
			} else if (WIFSIGNALED(status)) {
				// Change status to done, and remove child from array
				strcpy(shell_info->jobs_table[i].status, JOB_STATUS_DONE);
				// TODO: Send output to client
				printJob(i, shell_info);
				removeJob(i, shell_info);
			} else if (WIFSTOPPED(status)) {
				// Change status to stopped
				strcpy(shell_info->jobs_table[i].status, JOB_STATUS_STOPPED);
			} else if (WIFCONTINUED(status)) {
				// Change status to running
				strcpy(shell_info->jobs_table[i].status, JOB_STATUS_RUNNING);
			}
		}
	}
}


/**
 * \brief Send a SIGKILL to all jobs in the jobs list
 *
 * \param	shell_info	Shell info struct pointer
 */
void killAllJobs(shell_info_t *shell_info) {
	for (int i=0; i<shell_info->jobs_table_idx; i++) {
			// Skip jobs that already finished
			if (!strcmp(shell_info->jobs_table[i].status, JOB_STATUS_RUNNING) ||
					!strcmp(shell_info->jobs_table[i].status, JOB_STATUS_STOPPED)) {
				kill(shell_info->jobs_table[i].gpid, SIGKILL);
			}
	}
}


/**
 * \brief Point of entry.
 *
 * \param	job_str		Job raw string
 * \param	shell_info	Shell info struct pointer
 * \return	Errorcode
 */
int startJob(char *job_str, shell_info_t *shell_info) {
	char buf_time[BUFF_SIZE_TIMESTAMP];

	// Check input to ignore and show the prompt again
	if (shell_info->th_args->cmd_args.verbose) {
		fprintf(stderr, "%s yashd[%s:%d]: INFO: Checking if input should be "
				"ignored...\n", timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
				inet_ntoa(shell_info->th_args->from.sin_addr),
				ntohs(shell_info->th_args->from.sin_port));
	}

	// Check if input should be ignored
	if (ignoreInput(job_str)) {
		if (shell_info->th_args->cmd_args.verbose) {
			fprintf(stderr, "%s yashd[%s:%d]: INFO: Input ignored\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(shell_info->th_args->from.sin_addr),
					ntohs(shell_info->th_args->from.sin_port));
		}
	} else if (runShellCmd(job_str, shell_info)) {	// Check if input is a shell command
		if (shell_info->th_args->cmd_args.verbose) {
			fprintf(stderr, "%s yashd[%s:%d]: INFO: Ran shell command\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(shell_info->th_args->from.sin_addr),
					ntohs(shell_info->th_args->from.sin_port));
		}
	} else {	// Handle new job
		if (shell_info->th_args->cmd_args.verbose) {
			fprintf(stderr, "%s yashd[%s:%d]: INFO: New job\n",
					timeStr(buf_time, BUFF_SIZE_TIMESTAMP),
					inet_ntoa(shell_info->th_args->from.sin_addr),
					ntohs(shell_info->th_args->from.sin_port));
		}
		handleNewJob(job_str, shell_info);
	}

	// Check for finished jobs
	maintainJobsTable(shell_info);
	return (EXIT_OK);
}
