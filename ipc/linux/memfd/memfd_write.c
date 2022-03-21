#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <semaphore.h>

#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); } while (0)

#define K 1024
#define M (K*1024)

#define FILESIZE (2 * M)


struct memtext {
    sem_t sem_write;
    sem_t sem_read;
    char text[1];
};

int main(void){
    int fd;
    void *addr;

    fd = memfd_create("memfd_test", MFD_CLOEXEC);
    if (fd == -1){
        errExit("memfd_create");
    }

    if (ftruncate(fd, FILESIZE) == -1){
        errExit("truncate");
    }

    addr = mmap(NULL, FILESIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if(addr == MAP_FAILED){
        errExit("mmap");
    }

    struct memtext *mtext = addr;
    sem_init(&mtext->sem_write, 1, 0);
    sem_init(&mtext->sem_read, 1, 1);

    pid_t pid = fork();
    if(pid > 0){
        //parent
        sem_wait(&mtext->sem_read);
        sprintf(mtext->text, "C++\n");
        sem_post(&mtext->sem_write);

        sem_wait(&mtext->sem_read);
        sprintf(mtext->text, "java\n");
        sem_post(&mtext->sem_write);
        
        sem_wait(&mtext->sem_read);
        sprintf(mtext->text, "python\n");
        sem_post(&mtext->sem_write);

        sem_wait(&mtext->sem_read);
        sprintf(mtext->text, "go\n");
        sem_post(&mtext->sem_write);

        sem_wait(&mtext->sem_read); // 等待读结束 //sleep(1)
        sem_destroy(&mtext->sem_read);
        sem_destroy(&mtext->sem_write);
        fprintf(stderr, "parent exit!");
        exit(0);
        
    }else if(pid == 0){
        //child

        while(1){
            
            int res = sem_wait(&mtext->sem_write);
            if(res == EINVAL){
                fprintf(stderr, "child exit!");
                exit(0);
            }
            if(res == EAGAIN || res == EINTR){
                continue;
            }

            char *cur = mtext->text;
            printf("> ");
            while(*cur != '\0'){
                putchar(toupper(*cur));
                cur++;
            }
            sem_post(&mtext->sem_read);
        }
        
    }else{
        //error
        errExit("fork");
    }

    return 0;
}

