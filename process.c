// process.c                                 Phil Esterman (11/13/15)
//
// Processes command-line args for Bsh's backend.
#include "process.h"
#include <assert.h>

#define SUCCESS (0)
#define ERROR (1)

#define STDIN (0)
#define STDOUT (1)
#define STDERR (2)

//is it a built-in command? 
#define IS_BUILT(cmd) ((strcmp(cmd, "dirs") == 0) || \
						  (strcmp(cmd, "cd") == 0) || \
						  (strcmp(cmd, "wait") == 0))

//holds the commands, ordered,
//to execute for piping
struct pipe_chain {
	int n;    // # commands within
	int size; //total capicity
	CMD **cmd_list; //list of commands
};

typedef struct pipe_chain pipe_chain;

// EXECUTE class of command
int simple_cmd (CMD *cmd);
int stage_cmd (CMD *cmd);
int built_cmd (CMD *cmd);
int and_or_cmd (CMD *cmd);
int seq_cmd (CMD *cmd);

// PIPING helper and execution
int pipe_cmd (CMD *cmd);
void build_pipe_chain(CMD *cmd, struct pipe_chain *
							         my_pipe_chain);

//SET UP file descriptors to redirect IO
int set_red_out (CMD *cmd);
int set_red_in (CMD *cmd);

// EXECUTE a particular built-in command
int exec_dirs(void);
int exec_cd(CMD *cmd);
int exec_wait(void);



////////////// EXECUTE COMMANDS //////////////

// Execute a simple command, cmd. 
// Return SUCCESS or ERROR.
int simple_cmd(CMD *cmd)
{
	pid_t pid;
	int status = SUCCESS;

	if (!cmd) return status; //ensures given been given cmd

	if (IS_BUILT(cmd->argv[0]))
		status = built_cmd(cmd);
	else 
	{
		if( (pid = fork()) < 0)	//child process not created
		{
				perror("SIMPLE: ");
				return errno;
		}	
		else
		{
			if(pid == 0) //child process
			{

				if(cmd->fromType != NONE)
				{
					//setup input redirection
					int red_err = set_red_in(cmd);
					if(red_err != SUCCESS)
					{
						perror("RED_IN: ");
						exit(red_err);
					}
				}

				if(cmd->toType != NONE) 
				{
					//setup output redirection
					int red_err = set_red_out(cmd);
					if (red_err != SUCCESS)
					{
						perror("RED_OUT: ");
						exit(red_err);
					} 
				}

				execvp(cmd->argv[0], cmd->argv); //execute it 
				
				perror("SIMPLE: "); //print possible error
				_exit(errno); //exit to parent process
			}
			else // parent process
			{
				waitpid(pid, &status, 0);

				//updates status in case of sigint
				status = (WIFEXITED(status) ? WEXITSTATUS(status) 
								: 128+WTERMSIG(status));
			}

		}
	}

	return status;
}

int stage_cmd (CMD *cmd)
{
	pid_t pid; 
	int status = SUCCESS;
	
	if (!cmd) return status;
		
	if (cmd->type == SIMPLE)
		status = simple_cmd(cmd);

	else if (cmd->type == SUBCMD)
	{

		if( (pid = fork()) < 0)	//child process not created
		{
				perror("STAGE: ");
				return errno;
		}	
		else
		{
			if(pid == 0) //child process
			{	

				if(cmd->fromType != NONE)
				{
					//setup input redirection
					int red_err = set_red_in(cmd);
					if(red_err != SUCCESS)
					{
						perror("RED_IN: ");
						exit(red_err);
					}
				}

				if(cmd->toType != NONE) 
				{
					//setup output redirection
					int red_err = set_red_out(cmd);
					if (red_err != SUCCESS)
					{
						perror("RED_OUT: ");
						exit(red_err);
					} 
				}

				_exit(simple_cmd(cmd->left)); //exit to parent process
			}
			else // parent process
			{
				waitpid(pid, &status, 0);

				//updates status in case of sigint
				status = (WIFEXITED(status) ? WEXITSTATUS(status) 
								: 128+WTERMSIG(status));
			}

		}

	}


	return status;
}


int built_cmd (CMD *cmd)
{
	int status = SUCCESS;

	if (!cmd) return status;

	if(cmd->fromType != NONE)
	{
		//setup input redirection
		int red_err = set_red_in(cmd);
		if(red_err != SUCCESS)
		{
			perror("RED_IN: ");
			exit(red_err);
		}
	}

	if(cmd->toType != NONE) 
	{
		//setup output redirection
		int red_err = set_red_out(cmd);
		if (red_err != SUCCESS)
		{
			perror("RED_OUT: ");
			exit(red_err);
		} 
	}

	if (strcmp(cmd->argv[0], "dirs") == 0)
		status = exec_dirs();
	else if (strcmp(cmd->argv[0], "cd") == 0)
		status = exec_cd(cmd);
	else if (strcmp(cmd->argv[0], "wait") == 0)
		status = exec_wait();

	return status;
}

/////// PIPING ////////////

//build an ordered list of the piped commands. 
void build_pipe_chain(CMD *cmd, struct pipe_chain *
								my_pipe_chain)
{
	if(cmd->type == PIPE) //continue to left-most pipe	
	{	
		build_pipe_chain(cmd->left, my_pipe_chain);
		build_pipe_chain(cmd->right, my_pipe_chain);
	}
	else if (cmd->type != PIPE) //reached end
	{
		// grow command list!
		if(my_pipe_chain->n == my_pipe_chain->size)
			{
				my_pipe_chain->cmd_list = realloc(
					      my_pipe_chain->cmd_list, 
					     2 * my_pipe_chain->size);
				my_pipe_chain->size *= 2; 
			}

		my_pipe_chain->cmd_list[my_pipe_chain->n] = cmd;
		my_pipe_chain->n++;
	}
}



//Modeled from  from Stan Eisenstat's 
//pipe.c implementation
int pipe_cmd (CMD *cmd)
{	
	int overall_status = SUCCESS;

	if (cmd)
	{
	if (cmd->type != RED_PIPE)
		overall_status = stage_cmd(cmd);
	else
	{

	struct entry {
		int pid, status; 
	} *table; //table for (pid,status) of all ps

	int fd[2], //read, write fd's. 
	pid, status = SUCCESS, //ps ID and status for children
	fdin,
	i, j; //read in of last pipe (else-> STDIN)

	CMD *curr_cmd; //current command processing

	//initialize pipe_chain
	pipe_chain *my_pipe_chain = calloc(1, sizeof(*my_pipe_chain));
	my_pipe_chain->cmd_list = calloc(50, sizeof(CMD*));
	my_pipe_chain->n = 0;
	my_pipe_chain->size = 50;

	build_pipe_chain(cmd, my_pipe_chain);
	assert(my_pipe_chain->n >= 2);

	table = calloc(my_pipe_chain->n, sizeof(*table));

	fdin = 0;			 //original STDIN
	for(i = 0; i < my_pipe_chain->n - 1; i++) //the chain of ps 
	{											  //all but last
		
		if(pipe(fd) || (pid = fork()) < 0)
		{
			perror("PIPE: ");
			exit(ERROR);
		}

		else if(pid == 0)	//child		
		{
			close(fd[0]);	//so parents gets data from child
			if (fdin != 0)  //set stdin to the new pipe's read
			{
				dup2(fdin, 0);
				close(fdin);
			}
			if (fd[1] != 1) //set stdout to new pipe's write
			{
				dup2(fd[1], 1);
				close(fd[1]);
			}
			curr_cmd = my_pipe_chain->cmd_list[i];
			status = execvp(curr_cmd->argv[0], curr_cmd->argv);
			if (status < 0) status = ERROR;
			else status = SUCCESS;

			perror("PIPE CMD");
			_exit(status);
		}
		else // parent ps 
		{	
			table[i].pid = pid; 
			if (i > 1)
				close(fdin);	//???
			fdin = fd[0]; //remember the read from the pipe
			close(fd[1]); //don't write to pipe
		}
	}

	//the last ps! 
	curr_cmd = my_pipe_chain->cmd_list[my_pipe_chain->n-1];
	if ((pid = fork()) < 0)
	{
		perror("PIPE: ");
		_exit(ERROR);
	}
	else if (pid == 0)
	{
		if(fdin != 0)
		{
			dup2(fdin, 0); //make stdin  read[last pipe]
			close(fdin);
		}
		status = execvp(curr_cmd->argv[0], curr_cmd->argv);
		if (status < 0) status = ERROR;
		else status = SUCCESS;

		perror("PIPE CMD");
		_exit(status);
	}
	else
	{
		table[my_pipe_chain->n-1].pid = pid;
		if (i > 1)
			close(fdin);
	}

	for(i = 0; i < my_pipe_chain->n; )
	{
		pid = wait(&status);
		for (j = 0; j < my_pipe_chain->n &&
				table[j].pid != pid; j++)
			;
		if (j < my_pipe_chain->n)
		{
			table[j].status = status;
			i++;
		}
	}

	for (i = 0; i < my_pipe_chain->n; i++)
	{
		if (WIFEXITED(table[i].status))
		{
			overall_status = WEXITSTATUS(table[i].status);
		}
		else if (128+WTERMSIG(table[i].status))
			overall_status = 128+WTERMSIG(table[i].status);
	}

	free(table);
	free(my_pipe_chain->cmd_list);
	free(my_pipe_chain);

	}

	}


	return overall_status;
}



int and_or_cmd (CMD *cmd)
{	
	int status = SUCCESS; 

	if (!cmd) return status;
	
	if (cmd->type != SEP_AND && cmd->type != SEP_OR)
		status = pipe_cmd(cmd);
	else if (cmd->type == SEP_OR)
	{
		status = and_or_cmd(cmd->left);
		if (status == ERROR) //short circuit otherwise
			status = pipe_cmd(cmd->right);
	}
	else if (cmd->type == SEP_AND)
	{	
		status = and_or_cmd(cmd->left);
		if (status == SUCCESS)
			status = pipe_cmd(cmd->right);
	}
	
	return status;
}

int seq_cmd(CMD *cmd)
{	
	int status = SUCCESS; 	
	if (!cmd) return status;

	if (cmd->type != SEP_END && cmd->type != SEP_BG)
		status = and_or_cmd(cmd);
	else if (cmd->type == SEP_END)
	{

		status = seq_cmd(cmd->left);
		if (cmd->right)
			status = seq_cmd(cmd->right);
	}
	else if (cmd->type == SEP_BG)
	{
		pid_t pid; 

		if( (pid = fork()) < 0)	//child process not created
		{
			perror("FORK: ");
			return errno;
		}
		else if (pid == 0) //run first cmd in background
		{	
			fprintf(stderr, "Backgrounded: %d\n", getpid());
			status = seq_cmd(cmd->left);
			_exit(status);
		}
		else //run second in foreground, no wait.
		{	
			if(cmd->right)
				status = and_or_cmd(cmd->right);
		}
	}
	

	return status;
}


////////////// REDIRECTION //////////////


int set_red_out (CMD *cmd)
{
	int mode = 0; //how to open the file? (append or truncate?)
	int file = -1;

	if (cmd->toType == RED_OUT) //truncate first
		mode = (O_TRUNC | O_WRONLY | O_CREAT);

	else if (cmd->toType == RED_OUT_APP) //append to file
		mode = (O_APPEND | O_WRONLY | O_CREAT);

	///SET FILE as output source///

	file = open(cmd->toFile, mode, 0666);

	if (file < 0) return ERROR;

	dup2(file, STDOUT); //stdout now points to file
	close(file);

	return SUCCESS;
}

int set_red_in (CMD *cmd)
{
	int file; 

	file = open(cmd->fromFile, O_RDONLY);
	if (file < 0) return ERROR;

	dup2(file, STDIN);
	close(file);

	return SUCCESS;
}


////////////// EXEC BUILT IN COMMANDS //////////////


int exec_dirs (void)
{
	char *curr_dir = calloc(PATH_MAX, sizeof(char));
	getcwd(curr_dir, PATH_MAX);

	printf(curr_dir); //print the dir path to stdout
	printf("\n");

	if (curr_dir == NULL)
		return ERROR;

	free(curr_dir);
	return SUCCESS;
}


int exec_cd(CMD *cmd)
{	
	int status = SUCCESS;

	if (cmd->argc == 1) //cd to $HOME
		status = chdir(getenv("HOME"));
	else
		status = chdir(cmd->argv[1]);

	if (status != 0)
	{
		perror("chdir: ");
		return ERROR;
	}

	return status;
}

int exec_wait(void)
{
	int status;
	int pid; 

	while( (pid = waitpid(-1, &status, 0)) != -1)
		if (pid != 0)
			fprintf(stderr, "Completed: %d (%d)\n",	pid, status);

	return SUCCESS;
}


////////////// PROCESS //////////////



int process (CMD *cmdList)
{
	int status;
	char str_status[2];
	pid_t pid; 

	//reap zombies
	while ((pid = waitpid((pid_t)(-1), &status, WNOHANG)) > 0)
		fprintf(stderr, "Completed: %d (%d)\n", pid, status); 

	//set local variables
	for(int i = 0; i < cmdList->nLocal; i++) //each variable
		setenv(cmdList->locVar[i], cmdList->locVal[i], 1);

	status = seq_cmd(cmdList);
	// status = !status; //flip so that 0->failure 1->success 

	//set ? as status. 
	*str_status = (char)(48 + status);
	str_status[1] = 0; //null term

	setenv("?", str_status, 1);
	

	//unset local variables
	for(int i = 0; i < cmdList->nLocal; i++) //each variable
		status = unsetenv(cmdList->locVar[i]);


	return 0;
}

//NOTES & REFERENCES: 
//
//Some overall structure provided by Kush Patel's 
//implementation of a shell in C, at https://github.com/kushpatel
//
//Guidance on reaping zombies from 
//http://www.microhowto.info/howto/reap_zombie_processes_using_a_sigchld_handler.html
//
//pipe_cmd is guided by piping in c example by Doug Von at 
//https://github.com/dougvk
//


