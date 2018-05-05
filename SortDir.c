#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#define PATH_LEN 256
#define ULONG_LEN 10

struct btNode{
  unsigned long size;
  char path[PATH_LEN];
  //int inode;
  struct btNode *right, *left;
};

typedef struct btNode fileNode;

int done = 0;
int DEBUGRW = 0;
int DEBUG = 0;
int spdp[2]; //Sort end of Pipe, Directory end of Pipe
fileNode *root = NULL;
//fileNode *inoRoot = NULL;

void insert(fileNode **tree, unsigned long newSize, char newPath[PATH_LEN]){
  fileNode *temp = NULL;
  if(!(*tree)){
    temp = (fileNode*)malloc(sizeof(fileNode));
    temp->left = temp->right = NULL;
    temp->size = newSize;
    strcpy(temp->path, newPath);
    *tree = temp;
    return;
  }
  if(newSize <= (*tree)->size) insert(&(*tree)->left, newSize, newPath);
  else if(newSize > (*tree)->size) insert(&(*tree)->right, newSize, newPath);
}

/*
void inoInsert(fileNode **tree, int newInode){
  fileNode *temp = NULL;
  if(!(*tree)){
    temp = (fileNode*)malloc(sizeof(fileNode));
    temp->left = temp->right = NULL;
    temp->inode = newInode;
    *tree = temp;
    return;
  }
  if(newInode <= (*tree)->inode) inoInsert(&(*tree)->left, newInode);
  else if(newInode > (*tree)->inode) inoInsert(&(*tree)->right, newInode);
}

fileNode* search(fileNode** tree, int inode){
  if(!(*tree)) return NULL;
  if(inode == (*tree)->inode) return *tree;
  else if(inode < (*tree)->inode) search(&((*tree)->left), inode);
  else if(inode > (*tree)->inode) search(&((*tree)->right), inode);
}
*/

void printInorder(fileNode *tree){
  if(tree){
    printInorder(tree->left);
    printf("%lu\t%s\n", tree->size, tree->path);
    printInorder(tree->right);
  }
}

void deleteTree(fileNode *tree){
  if(tree){
    deleteTree(tree->left);
    deleteTree(tree->right);
    free(tree);
  }
}

void findFiles(char path[]){
  if(DEBUG) printf("Finding files\n");
  int fd;
  if((fd = open(path, O_RDONLY)) == -1){
    perror("ERROR: failed to find file\n");
    return;
  }

  struct dirent{
    int d_type;
    off_t d_off;
    unsigned short d_reclen;
    char d_name[PATH_LEN];
  };

  DIR *dirP;
  struct dirent *entP;
  char buf[BUFSIZ];
  int c;
  char d_type;
  struct stat statBuf;

  struct timespec sleepDuration; //sleep duration 0.25s
  sleepDuration.tv_sec = 0;
  sleepDuration.tv_nsec = 1;

  if(DEBUG) printf("Entering loop\n");
  for(;;){
    c = syscall(SYS_getdents, fd, buf, BUFSIZ);
    if(c < 1) break;
    if(DEBUG) printf("LOOP\n");
    entP = (struct dirent*)(buf);
    d_type = *(buf + entP->d_reclen - 1);

    if(DEBUG) printf("%s\n", entP->d_name);

    if(strcmp(entP->d_name, ".") && strcmp(entP->d_name, "..")){
      if(DEBUG) printf("In if(. && ..) statement\n");
      char tempPath[PATH_LEN];
      strcpy(tempPath, path);
      strcat(tempPath, "/");
      strcat(tempPath, entP->d_name);

      if((d_type == DT_DIR) || (d_type == 0)){
        if(DEBUG) printf("Found directory\n");
        findFiles(tempPath);
      }

      stat(tempPath, &statBuf);

      if((d_type != DT_LNK) && S_ISREG(statBuf.st_mode)){
        if(DEBUG) printf("Found regular file\n");
        /*
        if(statBuf.st_nlink > 1){
          if(DEBUG) printf("Multiple hard links\n");
          if(search(&inoRoot, statBuf.st_ino) == NULL){
            inoInsert(&inoRoot, statBuf.st_ino);
            if(DEBUG) printf("First occurence of file\n");
          }
          else{
             if(DEBUG) printf("Seen this file before\n");
             lseek(fd, entP->d_off, SEEK_SET);
             continue;
          }
        }
        */
        char tempSize[ULONG_LEN];
        sprintf(tempSize, "%lu", statBuf.st_size);
        write(spdp[1], tempSize, (sizeof(char)*ULONG_LEN));
        if(DEBUG) printf("Wrote size to pipe\n");
        write(spdp[1], tempPath, (sizeof(char)*PATH_LEN));
        if(DEBUG) printf("Wrote path to pipe\n");
        if(DEBUGRW) printf("Wrote size/path to pipe\n");
        if(DEBUG) printf("Sending signal...\n");
        kill(getppid(), SIGUSR1);
        if(DEBUG) printf("Sleeping for time given in sleepDuration");
        nanosleep(&sleepDuration, NULL);
        //sleep(1);
      }
    }
    lseek(fd, entP->d_off, SEEK_SET);
  }
  close(fd);
}

void addToTree(int sig){ //wrapper function for insert
  if(DEBUG) printf("Signal received by parent!\n");
  char buf[BUFSIZ];
  char nodeSize[ULONG_LEN];
  read(spdp[0], nodeSize, (sizeof(char)*ULONG_LEN));
  if(DEBUG) printf("Read size from pipe\n");
  char nodePath[PATH_LEN];
  read(spdp[0], nodePath, (sizeof(char)*PATH_LEN));
  if(DEBUG) printf("Read path from pipe\n");
  if(DEBUGRW) printf("Read size/path from pipe\n");
  insert(&root, atoi(nodeSize), nodePath);
  if(DEBUG) printf("Inserted node into tree and returning!\n");
}

void foundFiles(int sig){ //file finding process finished
  wait(NULL);
  done++;
}

int main(int argc, char *argv[]){
  pipe(spdp); //Sorting process End, Directory process End
  int pid = fork(); //stdin - fd=0; stdout - fd=1;
  int cpid = pid;
  if(pid == 0){ //child
    if(DEBUG) printf("Child process started\n");
    close(spdp[0]);
    findFiles(argv[1]);
    if(DEBUG) printf("Finished finding files\n");
    kill(getppid(), SIGUSR2);
    return 0;
  }
  else{ //parent
    if(DEBUG) printf("Parent process branched\n");
    close(spdp[1]);
    struct sigaction sa1;
    sa1.sa_handler = addToTree;
    sa1.sa_flags = 0;
    sigemptyset(&sa1.sa_mask);
    sigaction(SIGUSR1, &sa1, NULL);
    struct sigaction sa2;
    sa2.sa_handler = foundFiles;
    sa2.sa_flags = 0;
    sigemptyset(&sa2.sa_mask);
    sigaction(SIGUSR2, &sa2, NULL);
    /*
    struct sigaction sa3;
    sa3.sa_handler = foundFiles;
    sa3.sa_flags = 0;
    sigemptyset(&sa3.sa_mask);
    sigaction(SIGCHLD, &sa3, NULL);

    while((pid = waitpid(-1, NULL, WNOHANG)) != cpid){
      if(pid == -1){
        perror("ERROR: while waiting for child to exit\n");
        return 1;
      }
      else if(DEBUG && (pid == 0)) printf("Child process with pid %d not found\n", cpid);
      pause();
    }
    */
    while(!done) pause();
    if(DEBUG) printf("Printing tree\n");
    printInorder(root);
    if(DEBUG) printf("Deleting tree\n");
    deleteTree(root);
    return 0;
  }
}

