#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h> 
#include <sys/stat.h> 
#include <mqueue.h>

#define MSGSIZE 8192


int main(void){

    const char testbuf[] = "hello mq";
    size_t bufsize = sizeof(testbuf);

    mqd_t mymq = mq_open("/testmq", O_CREAT| O_RDWR, 00600, NULL);
    if(mymq == -1){
        perror("mq_open failed.");
        exit(-1);
    }

#if 1
    char recv_buf[MSGSIZE];

    ssize_t recv_size;
    while( (recv_size = mq_receive(mymq, recv_buf, MSGSIZE, NULL)) > 1){
        printf("recv %s\n", recv_buf);
        fflush(stdout);
    }
    printf("read ok\n");

#endif


    mq_close(mymq);
//    mq_unlink("/testmq");

    return 0;
}
