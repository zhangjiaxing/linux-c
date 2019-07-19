#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <ctype.h>
#include "connection.h"


int main(void){

    struct sockaddr_un svaddr = {
        .sun_family = AF_UNIX,
        .sun_path = SERVER_SOCKET_NAME
    };
    
    unlink(SERVER_SOCKET_NAME);
    int sd = socket(AF_UNIX, SOCK_DGRAM, 0);

    if (sd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int ret = bind(sd, (const struct sockaddr *) &svaddr, sizeof(svaddr));
    if (ret == -1) {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    char buffer[100];
    socklen_t address_len = sizeof(struct sockaddr_un);
    struct sockaddr_un claddr;

    while(true){
        ssize_t size = recvfrom(sd, buffer, sizeof(buffer), 0, (struct sockaddr*)&claddr, &address_len);

        printf("recv size: %d, %d, %s, %s \n", size, claddr.sun_family, &claddr.sun_path, buffer);

        for(int i=0; i < size-1; i++){
            buffer[i] = toupper(buffer[i]);
        }
        size = sendto(sd, buffer, size, 0, (struct sockaddr*)&claddr, address_len);
        printf("sendto size: %d \n", size);
    }
    
    return 0;
}
