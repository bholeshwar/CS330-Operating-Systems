#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

void recMyGrep(char*, char*);
void myGrep(char*, char*);
void singMyGrep(char*, char*);

int main(int argc, char *argv[]) {
    if(argc!=3) {
    	printf("Enter valid number of arguments\n");
    	exit(-1);
    }

    char *path = argv[2];
    char *key = argv[1];

    //removing extra / if present at the end of path
    if(path[strlen(path)-1] == '/') {
      path[strlen(path)-1] = '\0';
    }

    // checking if it is a valid file/directory
    struct stat sb;
    if(stat(path, &sb) < 0) {
      perror("Invalid directory/file");
      exit(-1);
    }

    else {
      // if it is a regular file
      if((sb.st_mode & S_IFMT) == S_IFREG) {
          singMyGrep(path, key);
      }
      // if it is a directory
      else {
          recMyGrep(path, key);
      }
    }
}

// recursively doing grep in every sub-directory/file
void recMyGrep(char *path, char *key) {
    struct dirent *dp;
    DIR *dir = opendir(path);

    // Unable to open directory
    if (dir==NULL) {
      return;
    }

    while ((dp = readdir(dir)) != NULL) {
        if (strcmp(dp->d_name, ".") != 0 && strcmp(dp->d_name, "..") != 0) {
            
            char *path2 = (char*)malloc((strlen(path) + strlen(dp->d_name) + 2)*sizeof(char));

            strcpy(path2, path);
            strcat(path2, "/");
            strcat(path2, dp->d_name);

            if(dp->d_type == DT_REG) { // regular files
                myGrep(path2, key);
            }

            recMyGrep(path2, key);
        }
    }

    closedir(dir);
}

void myGrep(char *path, char *key) {
    struct stat sb;
    int dump = stat(path, &sb);
    int siz = sb.st_size;

    int fd = open(path, O_RDONLY);

    char *rb = (char*)malloc(siz*sizeof(char));

    int bytes_read = 1;
    int k = 0;
    char t;

    while(bytes_read!=0) {
        bytes_read = read(fd, &t, 1);
        rb[k++] = t;
        if(t == '\n') {
            rb[k] = '\0';

            if(strstr(rb, key)!=NULL) {
                printf("%s: %s", path, rb);
            }
            k = 0;
        }
    }

    close(fd);
}

void singMyGrep(char *path, char *key) {
    struct stat sb;
    int dump = stat(path, &sb);
    int siz = sb.st_size;

    int fd = open(path, O_RDONLY);

    char *rb = (char*)malloc(siz*sizeof(char));

    int bytes_read = 1;
    int k = 0;
    char t;

    while(bytes_read!=0) {
        bytes_read = read(fd, &t, 1);
        rb[k++] = t;
        if(t == '\n') {
            rb[k] = '\0';

            if(strstr(rb, key)!=NULL) {
                printf("%s", rb);
            }
            k = 0;
        }
    }

    close(fd);
}