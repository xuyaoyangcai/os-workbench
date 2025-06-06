#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/syscall.h>
#include <regex.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>

int die=0;

void handler(int sig){
    die = 1;
    // printf("done");
    // wait(NULL);
    // exit(EXIT_SUCCESS);
}

char paths[256][512];
int len;
regex_t storage;

void get_path(){
    char *path = getenv("PATH");

    int i=0,count = 0, pstart=0;

    for(char *p=path;*p!=0;p++){
        count++;
        if(*p==':'){

            strncpy(paths[i], path+pstart, count-1);
            i++;
            pstart=p-path+1;
            count=0;
        }
    }
    len = i;
}

void match(char *line){
    size_t nmatch = 2;
    regmatch_t pmatch[nmatch];
    int ret=0;
    if((ret=regexec(&storage, line, nmatch, pmatch, 0))!=REG_NOMATCH){

        char buf2[4096];

        strncpy(buf2, line+pmatch[1].rm_so,pmatch[1].rm_eo);
        buf2[line+pmatch[1].rm_so,pmatch[1].rm_eo] = 0;
        printf("%s\n", buf2);
    }
}


int main(int argc, char *argv[], char *env[]) {
    regcomp(&storage,"^(.*?)\\(.*?\\) += [0-9?\\-]+.*?$", REG_NEWLINE|REG_EXTENDED);

    signal(SIGCHLD, handler);

    get_path();

    int pipefd[2];

    if(pipe(pipefd)<0)exit(EXIT_FAILURE);

    int pid;



    if((pid=fork())==0){ // child

        argv[0] = "strace";
        close(pipefd[0]); // close read fd
        dup2(pipefd[1], STDERR_FILENO);

        for(int i=0;i<len;i++){
            char buf[512];
            strcpy(buf,paths[i]);

            if(execve(strcat(buf,"/strace"),argv, env )<0){
                continue;
            }

        }

    }

    close(pipefd[1]); // close write


    char buf[1<<16];
    char *line;
    while(die!=1){

        sleep(1);
        size_t count;
        do{

            count = read(pipefd[0], buf,1<<16);
            int offect=0;
            // write(STDOUT_FILENO,buf,count);

            while(offect<count){
                line = buf+offect;
                int i=0;
                for(;line[i]!='\n';i++){
                    offect++;
                }
                offect++;
                line[i]=0;
                // printf("%s", line);
                match(line);
            }




        }while(count);


        if(waitpid(pid, NULL,WNOHANG )==pid)exit(0);


    }



}
