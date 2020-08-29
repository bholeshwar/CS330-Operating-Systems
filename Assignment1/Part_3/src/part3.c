/*
If there are N number of immediate sub-directories, this program forks N processes. In the child process, we calculate the size of the i(th) sub-directory in i(th) process and write this size in pipe.
In the i(th) parent process, we read the size of i(th) sub-directory from the pipe and add it to a variable root_size (which stores the size of root). Moreover, if we find an immediate file in the root directory,
we add its size to the root_size. At last we print root_size. The size of i(th) immediate sub-directory is printed in its own process.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

long findSizeOfDirectory(char*);
long findSizeOfFile(char*);
void printSizes(char*);
char *dirName(char*);

int main(int argc, char *argv[]) {
	if(argc!=2) {
		printf("Invalid number of arguments\n");
	}
    printSizes(argv[1]);
}

char *dirName(char *path) {
    
    if(path[strlen(path)-1] == '/') {
      path[strlen(path)-1] = '\0';
    }

    int l = strlen(path);
    int i;
    int k = 0;

    for(i=l-1; i>=0; i--) {
        if(path[i]=='/') {
            break;
        }
    }

    char *name = (char*)malloc((l-i)*sizeof(char));
    for(int j=i+1; j<l+1; j++) {
        name[k++] = path[j];
    }

    return name;
}

void printSizes(char *path) {

    struct dirent *dp; // pointer to items in directory dir
    DIR *dir = opendir(path);
    long int root_size = 0; // size of root directory

    if(dir==NULL) { // Unable to open directory
        return;
    }

    while((dp = readdir(dir)) != NULL) {

        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {

            char *path2 = (char*)malloc((strlen(path) + strlen(dp->d_name) + 2)*sizeof(char));

            strcpy(path2, path);
            strcat(path2, "/");
            strcat(path2, dp->d_name);

            if(dp->d_type == DT_DIR) { // sub-directory
                int fd[2];

                if(pipe(fd) < 0) { // Make a pipe of fd[2]
                    perror("pipe");
                    exit(-1);
                }

                int pid = fork();

                if(pid<0) { // unable to fork
                    perror("fork");
                    exit(-1);
                }

                else if(pid==0){ //child process where we calculate size of the sub-directory and the write end of the pipe stores the size of this sub-directory
                    long int s = findSizeOfDirectory(path2); // size of the sub-directory
                    printf("%s ", dp->d_name); // directory name
                    printf("%ld\n", s);

                    close(fd[0]); // closing read end of pipe

                    int temp1 = write(fd[1], &s, sizeof(s)); // writing size of the directory in the pipe

                    exit(1); // exit child
                }

                else {
                    wait(NULL); // wait for the child process to finish
                    long int t; 
                    close(fd[1]); // closing write end of pipe
                    int temp2 = read(fd[0], &t, sizeof(t)); // reading the pipe

                    root_size += t; // adding the size of the read sub directory to the root's size
                }
            }

            else { // not a sub-directory
                root_size += findSizeOfFile(path2);
            }
        }


    }   

    closedir(dir);

    printf("%s %ld\n", dirName(path), root_size);

}

long findSizeOfFile(char *path) {
    struct stat sb;
    int t = stat(path, &sb);

    return sb.st_size; // size of file
}

long findSizeOfDirectory(char *path) {
    struct dirent *dp;
    DIR *dir = opendir(path);

    long int siz = 0;

    if(dir==NULL) {
        return 0;
    }

    while ((dp = readdir(dir)) != NULL) { // 
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {

            char *path2 = (char*)malloc((strlen(path) + strlen(dp->d_name) + 2)*sizeof(char));

            strcpy(path2, path);
            strcat(path2, "/");
            strcat(path2, dp->d_name);

            struct stat sb;
            int t = stat(path2, &sb);

            // Searches for directories
            if((sb.st_mode & S_IFMT) == S_IFDIR) {
                siz += findSizeOfDirectory(path2); // recursively find size of all the sub-directories
            }
            else {
                siz += sb.st_size; // add the size of file
            }
        }
    }

    closedir(dir);

    return siz;
}