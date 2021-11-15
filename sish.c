#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#define MAX_ARGS 20

//I read character by character and resize by 10 just in case
char* safeDynamicRead(){

	char* readIn;
	int index=0;
	char curr;
	int alloc=10;

	readIn=(char *)malloc(alloc*sizeof(char));
	if(readIn==NULL){
		printf("Allocation of command line failed");
		return readIn;
	}

	curr=getchar();
	while((curr!=EOF)&&(curr!='\n')){
		readIn[index]=curr;
		curr=getchar();
		index++;
		if(index==alloc){
			alloc++;
			readIn=realloc(readIn,sizeof(*readIn)*alloc);
			if(readIn==NULL){
				printf("Reallocation of command line failed");
				return readIn;
			}
		}
	}
	readIn[index]='\0';
	return readIn;
}

char*  safeStrCopy(char* input){
	char* newStr;
	newStr=(char *)malloc((strlen(input))*sizeof(char));
	int index=0;
	while(*(input+index)){
		*(newStr+index)=*(input+index);
		index++;
	}
	return newStr;
}

void cmdParse(char* input, char** args){
	int index=0;
	char delimiter[]={32};//Our delimiter specified by the requirements
	char* token;
	token=strtok(input,delimiter);

	while((token!=NULL)&&(index<MAX_ARGS-1)){
		args[index]=token;
		index++;
		token=strtok(NULL, delimiter);
	}
	args[index]=NULL;
}

void cmdExec(char** args){
	pid_t cpid;
	int err;
	cpid=fork();

	if(cpid==-1){
		printf("Error when forking");
		return;
	}
	if(cpid==0){
		err=execvp(args[0],args);
		if(err<0){
			printf("Invalid Command or Argument\n");
		}
	}else{
		wait(NULL);//wait for child  process to complete
	}
}

void cmdExecPipeline(int numPipes, char* pipeline){
	////Store commands in local variable and safe copy as strings
	char* cmds[numPipes+1];//we add an extra space for the command at the pipeline tail
	int index=0;
	while(pipeline!=NULL){
		cmds[index]=safeStrCopy(pipeline);
		index++;
		pipeline=strtok(NULL,"|");
	}
	//////////////////////////////////
	
	/////Establish our pipeline resources/////////////////////
	int plumber,pipefd[2];//The plumber that handles our piping
	////////////////////////////////////////////////
	

	/////////Begin Pipeline Operation/////////////
	int i;
	int err;
	plumber=STDIN_FILENO;//Our plumber has his hand on the input
	///We handle each pipe operation one by one///
	for(i=0;i<numPipes;i++){
		pipe(pipefd);

		pid_t child;

		child=fork();
		if(child<0){
			printf("Fork Failed\n");
			return;
		}
		if(child==0){
			//If the command is not the head of the pipe, the plumber will leave it as is
			//otherwise, the plumber will connect the next head to the previous tail
			if(plumber!=STDIN_FILENO){
				dup2(plumber,STDIN_FILENO);
				close(plumber);
			}

			dup2(pipefd[1],STDOUT_FILENO);//Set STDout to current pipe (so the next input can receive)
			close(pipefd[1]);

			char* args[MAX_ARGS];
			cmdParse(cmds[i],args);

			err=execvp(args[0],args);
			if(err<0){
				printf("Invalid Command or Arguments");
				return;
			}
		}
		//Set up for next child
		close(plumber);
		close(pipefd[1]);
		//connect the tail to next head
		plumber=pipefd[0];
	}
	//Execute the last command
	if(plumber!=STDIN_FILENO){
		dup2(plumber,STDIN_FILENO);
		close(plumber);
	}
	char* args[MAX_ARGS];
	cmdParse(cmds[i],args);
	
	err=execvp(args[0],args);
	if(err<0){
		printf("Invalid COmmand or Arguments");
		return;
	}
	for(i=0;i<numPipes+1;i++){
		free(cmds[i]);//Free Dynamic memory that is used
	}
}

//Can execute two commands piped
void cmdExecPiped(char* pipeline){
	///////////Commands from the pipeline//////////////
	char* cmd1;
	char* cmd2;
	cmd1=safeStrCopy(pipeline);
	cmd2=strtok(NULL,"|");
	///////////////////////////////////////////////////
	
	//////////Arguments Passed to Execvp///////////////
	char* args1[MAX_ARGS];
	char* args2[MAX_ARGS];
	
	cmdParse(cmd1,args1);
	cmdParse(cmd2,args2);
	///////////////////////////////////////////////////	
	//////////////Pipe Resources///////////////////////
	int pipefd[2];
	pid_t cpid1,cpid2;
	int err1,err2,err3;

	err3=pipe(pipefd);
	if(err3<0){
		printf("Pipe Failed");
		return;
	}
	//////////////////////////////////////////////////

	/////////////Begin Pipe Operation/////////////////
	cpid1=fork();
	if(cpid1<0){
		printf("Fork Failed");
		return;
	}
	if(cpid1==0){
		close(pipefd[0]);//We do not need the read end for the first half of the pipe
		dup2(pipefd[1],STDOUT_FILENO);
		close(pipefd[1]);//After duping the write, we no longer need it in this context
		
		err1=execvp(args1[0],args1);
		if(err1<0){
			printf("Invalid Command Or Argument");
		}
	}
	cpid2=fork();
	if(cpid2<0){
		printf("Fork Failed");
	}
	if(cpid2==0){
		close(pipefd[1]);//We do not need the write end for the second part of the pipe
		dup2(pipefd[0],STDIN_FILENO);
		close(pipefd[0]);//After duping the read, we no longer reference it in this context
	
		err2=execvp(args2[0],args2);
		
		if(err2<0){
			printf("Invalid Command Or Argument");
		}
	}
	
	close(pipefd[0]);
	close(pipefd[1]);
	
	waitpid(cpid1,NULL,0);
	waitpid(cpid2,NULL,0);
	
	//////////////////////////////////////////////////
	
	//command1 gets deallocated for safety
	free(cmd1);
}

int pipePresent(char* input){
	int index;
	int pipes=0;
	for(index=0;index<strlen(input);index++){
		if(input[index]=='|'){
			pipes++;
		}
	}
	return pipes;
}

int main(){
	//We insist on reading a line until we break out
	int myBool; 

	char *history[100];
	int histIndex=0;
	char* tokenizer;
	int myExit=1;
	
	while(myExit){
		char* commandInput;
		printf("sish> ");
		commandInput=safeDynamicRead();//We do a dynamic read since it is required. I know that I could use getline, but it would be dangerous
		history[histIndex]=safeStrCopy(commandInput);
		tokenizer=strtok(commandInput," ");
		
		while(tokenizer!=NULL){

			myBool=strcmp(tokenizer,"exit");
			if(myBool==0){
				int i;
				for(i=0;i<histIndex;i++){
					free(history[i]);
				}
				myExit=0;
			}else{
				myBool=strcmp(tokenizer,"history");
				if(myBool==0){
					tokenizer=strtok(NULL,commandInput);

					if(tokenizer!=NULL){
						int i;
						int tempBool;

						tempBool=strcmp(tokenizer,"-c");
						if(tempBool==0){
							for(i=0;i<histIndex;i++){
								free(history[i]);
							}
							histIndex=0;
						}else{
							int offset=-1;
							sscanf(tokenizer,"%d",&offset);
							if((offset>histIndex)||(offset<0)){
								printf("Invalid argument: history\n");
							}else{
								for(i=0;i<offset;i++){
									printf("%i", i);
									printf("\t");
									printf(history[i]);
									printf("\n");
								}
							}
							if(histIndex<99){
								histIndex++;
							}
						}
						
					}else{
						int i;
						for(i=0;i<histIndex;i++){
							printf("%i", i);
							printf("\t");
							printf(history[i]);
							printf("\n");
						}
						if(histIndex<99){
							histIndex++;
						}
					}
				}else{
					myBool=strcmp(tokenizer,"cd");
					if(myBool==0){
						int err;
						tokenizer=strtok(NULL,commandInput);
						int tempBool=strcmp(tokenizer, "..");
						if(tempBool==0){
							chdir("..");
						}else{
							err=chdir(tokenizer);
							if(err<0){
								printf("Invalid directory\n");
							}
						} 
					}else{
						//////////////////NEW STUFF///////////////////

						//Here we clear out the tokenizer
						while(tokenizer!=NULL){
							tokenizer=strtok(NULL,commandInput);
						}
						char* notBuiltIn;
						char* pipedCommands;
						notBuiltIn=safeStrCopy(history[histIndex]);

						int pipeflag=0;
						pipeflag=pipePresent(notBuiltIn);
						pipedCommands=strtok(notBuiltIn,"|");
						
						if(pipeflag>0){
							pid_t pipeHandler;
							pipeHandler=fork();
							if(pipeHandler<0){
								printf("Fork Failed\n");
								return(0);
							}
							if(pipeHandler==0){
								cmdExecPipeline(pipeflag,pipedCommands);
							}
							waitpid(pipeHandler,NULL,0);
						}else{
							char* myArgs[MAX_ARGS];
							cmdParse(pipedCommands,myArgs);
							cmdExec(myArgs);
						}

						//free dynamic memory
						free(notBuiltIn);
					}
					if(histIndex<99){
						histIndex++;
					}
				}
			}
			if(tokenizer!=NULL){
				tokenizer=strtok(NULL,commandInput);
			}
		}
		free(commandInput);//I free the command line to save space
	}
	return(0);
}
