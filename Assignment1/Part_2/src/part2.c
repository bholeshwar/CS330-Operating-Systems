#include <stdio.h> 
#include <stdlib.h> 
#include <string.h>
#include <fcntl.h> 
#include <errno.h> 
#include <sys/wait.h> 
#include <unistd.h> 

void task1(char* key, char* path);
void task2(char* grep_path, char* grep_key, char* tee_path, char** cmd);

int main(int argc, char *argv[]){ 
    if (strcmp(argv[1], "@") == 0) {
        task1(argv[2], argv[3]);
    }

    else if(strcmp(argv[1], "$") == 0) {
        char **cmd = (char**)malloc((argc-5+1)*sizeof(char*));
        int i;

        for(i=0; i<argc-5; i++) {
            cmd[i] = argv[i+5];
        }
        cmd[i] = NULL;

        task2(argv[3], argv[2], argv[4], cmd);
    }

    else {
        printf("Command not found");
        exit(-1);
    }
    
    return 0;
}

void task1(char *key, char *path) {
    // wc -l is parent, grep -rF is child

    int fd[2]; 
    
    if(pipe(fd)<0) {
        perror("pipe");
        exit(-1);
    }

    int pid = fork();

    if(pid<0) {
        perror("fork");
        exit(-1);
    }
  
    else if(pid==0) // child process
    { 
        // making stdout same as fd[1] 
        dup2(fd[1], 1); 

        close(fd[0]);
        close(fd[1]);
        
        //executing grep -rF
        execl("/bin/grep","grep","-rF", key, path, (char*)NULL);
    } 

    else // parent process
    { 
        // making stdin same as fd[0] 
        dup2(fd[0], 0); 

        close(fd[1]);
        close(fd[0]);
                  
        // executing wc -l
        execl("/usr/bin/wc", "wc", "-l", (char*)NULL);
    } 
}

void task2(char* grep_path, char* grep_key, char* tee_path, char **cmd) {
    // cmd is parent, tee is child, grep -rF is grandchild

    int fd[2];    // pipe between parent and child
    int fd1[2];   // pipe between child and grandchild
    int pid, pid2;

    if(pipe(fd)<0) {
        perror("pipe");
        exit(-1);
    }

    if((pid = fork()) < 0) {
        perror("fork");
        exit(-1);
    }

    else if(pid==0) { // We are inside tee
        if(pipe(fd1)<0) {
            perror("pipe");
            exit(-1);
        }
        else {
            if((pid2 = fork()) < 0) {
                perror("fork");
                exit(-1);
            }

            else if(pid2==0) { // We are inside grep -rF
                dup2(fd1[1], 1);
                close(fd1[0]);
                close(fd1[1]);
                execl("/bin/grep","grep","-rF", grep_key, grep_path, (char*)NULL);
                exit(1);        
            }

            else { // We are inside tee
                dup2(fd1[0], 0);
                dup2(fd[1], 1);
                close(fd1[0]);
                close(fd1[1]);
                char c;

                // function for tee
                int fptr = open(tee_path, O_WRONLY | O_TRUNC | O_CREAT, 0640);
                while(read(0, &c, 1) > 0){
                    write(1, &c, 1);
                    write(fptr, &c, 1);
                }
                close(fptr);

                exit(2);
            }
        }
        exit(3);
    }

    else { // We are inside cmd
        dup2(fd[0], 0);
        close(fd[0]);
        close(fd[1]);

        char *cmd_path = (char*)malloc((strlen(cmd[0]) + 6)*sizeof(char));
        strcat(cmd_path, "/bin/");
        strcat(cmd_path, cmd[0]);

        execvp(cmd_path, cmd);
        exit(4);
    }
}