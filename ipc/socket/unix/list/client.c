#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdbool.h>

#include "connection.h"


int main(void){

    struct sockaddr_un svaddr = {
        .sun_family = AF_UNIX,
        .sun_path = SERVER_SOCKET_NAME
    };

    struct sockaddr_un claddr = {
        .sun_family = AF_UNIX
    };

    
    int sd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }


    snprintf(claddr.sun_path, sizeof(claddr.sun_path), "/tmp/client.%ld.socket", (long) getpid());
    printf("claddr.sun_path = %s\n", claddr.sun_path);
    
    int ret = bind(sd, (const struct sockaddr *)&claddr, sizeof(claddr));
    if(ret == -1){
        perror("bind");
        exit(EXIT_FAILURE);
    }

    char buffer[100] = "hello";
    ssize_t size = sendto(sd, buffer, strlen(buffer)+1, 0, (const struct sockaddr *) &svaddr, sizeof(svaddr));

    printf("sendto size: %d, %s\n", size, buffer);

    while(true){
        size =  recvfrom(sd, buffer, sizeof(buffer), 0, NULL, NULL);
        printf("recv size: %d, %s \n", size, buffer);
    }
    unlink(claddr.sun_path);
    
    return 0;
}
