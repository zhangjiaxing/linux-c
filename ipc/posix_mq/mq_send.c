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

    for(int i=1; i<100; i++){

        if (mq_send(mymq, testbuf, bufsize, 0) != 0){
            perror("mq_send.");
            exit(-1);
        }
        printf("%d\n", i);
    }


    mq_close(mymq);
//    mq_unlink("/testmq");

    return 0;
}
