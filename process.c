// process.c                                 Phil Esterman (11/13/15)
//
// Processes command-line args for Bsh's backend.
#include "process.h"

#define SUCCESS (0)
#define ERROR (1)

#define STDIN (0)
#define STDOUT (1)
#define STDERR (2)

// EXECUTE class of command
int simple_cmd (CMD *cmd);
int stage_cmd (CMD *cmd);

//SET UP file descriptors to redirect IO
int set_red_out (CMD *cmd);
int set_red_in (CMD *cmd);



////////////// EXECUTE COMMANDS //////////////


int simple_cmd(CMD *cmd)
{
	pid_t pid;
	int status = SUCCESS;

	if (cmd && cmd->type == SIMPLE)
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




////////////// PROCESS //////////////



int process (CMD *cmdList)
{
	int status;

	//set local variables
	for(int i = 0; i < cmdList->nLocal; i++) //each variable
	{
		status = setenv(cmdList->locVar[i], cmdList->locVal[i], 1);
		if (status != SUCCESS) perror("setenv: ");
	}


	// for(int i = 0; i < cmdList->nLocal; i++) //each variable
	// 	for(int k = 0; k < cmdList->argc; k++)
	// 		if (strcmp(cmdList->argv[k], cmdList->locVar[i]) == 0)
	// 			cmdList->argv[k] = cmdList->locVal[i];



	stage_cmd(cmdList);
	
	//unset local variables
	for(int i = 0; i < cmdList->nLocal; i++) //each variable
	{
		status = unsetenv(cmdList->locVar[i]);
		if (status != SUCCESS) perror("unsetenv: ");
	}




	return 0;
}

