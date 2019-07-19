#include <stdio.h>

#include <unistd.h>
#include <stdlib.h>

#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <semaphore.h>


int main(void){

    sem_t* mysem  = sem_open("testsem", O_CREAT, 0600, 2);
    if(! mysem){
        perror("sem_open failed\n");
        exit(0);
    }else{
        printf("sem open ok\n");
    }

    if(sem_wait(mysem) == 0){printf("sem wait ok\n");}
    if(sem_wait(mysem) == 0){printf("sem wait ok\n");}
    if(sem_post(mysem) == 0){printf("sem post ok\n");}else { printf("sem post err\n"); }
    sem_close(mysem);
//    sem_unlink("testsem");

//    sleep(200);
    
    return 0;
}

