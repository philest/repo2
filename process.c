// process.c                                 Phil Esterman (11/13/15)
//
// Processes command-line args for Bsh's backend.
#include "process.h"

#define SUCCESS (0)
#define ERROR (1)

#define STDIN (0)
#define STDOUT (1)
#define STDERR (2)

//is it a built-in command? 
#define IS_BUILT(cmd) ((strcmp(cmd, "dirs") == 0) || \
						  (strcmp(cmd, "cd") == 0) || \
						  (strcmp(cmd, "wait") == 0))

// EXECUTE class of command
int simple_cmd (CMD *cmd);
int stage_cmd (CMD *cmd);
int built_cmd (CMD *cmd);
int and_or_cmd (CMD *cmd);

//SET UP file descriptors to redirect IO
int set_red_out (CMD *cmd);
int set_red_in (CMD *cmd);

// EXECUTE a particular built-in command
int exec_dirs(void);
int exec_cd(CMD *cmd);
int exec_wait(void);



////////////// EXECUTE COMMANDS //////////////


int simple_cmd(CMD *cmd)
{
	pid_t pid;
	int status = SUCCESS;

	if (cmd)
	{		
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
					exit(errno); //exit to parent process
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
	}

	return status;
}

int stage_cmd (CMD *cmd)
{
	pid_t pid; 
	int status = SUCCESS;

	if (cmd)
	{
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
	}

	return status;
}


int built_cmd (CMD *cmd)
{
	int status = SUCCESS;

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

int pipe_cmd (CMD *cmd)
{
	if (cmd)
	{
		if (cmd->type != RED_PIPE)
			status = stage_cmd(cmd);
		{
			
		}

	}

}

int and_or_cmd (CMD *cmd)
{
	if (cmd)
	{
		if (cmd->type != SEP_AND && cmd->type != SEP_OR)
			status = pipe_cmd(cmd);
		else
		{

		}
	}

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

	while( (status = waitpid(-1, &status, 0)) != -1)
		if (status != 0)
		{
			perror("wait: ");
			return ERROR;
		}

	return SUCCESS;
}


////////////// PROCESS //////////////



int process (CMD *cmdList)
{
	int status;
	char str_status[1];

	//set local variables
	for(int i = 0; i < cmdList->nLocal; i++) //each variable
		setenv(cmdList->locVar[i], cmdList->locVal[i], 1);

	status = stage_cmd(cmdList);

	//set ? as status. 
	*str_status = (char)(48 + status);
	setenv("?", str_status, 1);
	
	//unset local variables
	for(int i = 0; i < cmdList->nLocal; i++) //each variable
		status = unsetenv(cmdList->locVar[i]);




	return 0;
}

