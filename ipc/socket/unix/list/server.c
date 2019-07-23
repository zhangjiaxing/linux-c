#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <ctype.h>
#include "list.h"
#include "connection.h"

struct sockaddr_node{
    struct list_head head;
    struct sockaddr_un addr;
};
typedef struct sockaddr_node sockaddr_node_t;
typedef struct sockaddr_node sockaddr_list_t;


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

    sockaddr_list_t addr_list;
    INIT_LIST_HEAD(&addr_list.head);


    while(true){
        sockaddr_node_t *addr_node = malloc(sizeof(sockaddr_node_t));
        socklen_t address_len = sizeof(struct sockaddr_un);
            
        ssize_t recv_size = recvfrom(sd, buffer, sizeof(buffer), 0, (struct sockaddr*)&addr_node->addr, &address_len);
        if(recv_size < 0)
            continue;
        
        sockaddr_node_t *pos;
        bool isfind = false;
        list_for_each_entry(pos, &addr_list.head, head){
            if(strcmp(pos->addr.sun_path, addr_node->addr.sun_path) == 0){
                isfind = true;
            }
        }
        if(! isfind){
            list_add_tail(&addr_node->head, &addr_list.head);
            printf("find new client \n");
        }else{
            printf("client already exist \n");
        }

        printf("recv size: %d, %d, %s, %s \n", recv_size, addr_node->addr.sun_family, addr_node->addr.sun_path, buffer);

        for(int i=0; i < recv_size-1; i++){
            buffer[i] = toupper(buffer[i]);
        }

        sleep(1);
        int len=0;
        
        sockaddr_node_t *n;
        list_for_each_entry_safe(pos, n, &addr_list.head, head){

     //   list_for_each_entry(pos, &addr_list.head, head){
            len++;
            ssize_t send_size = sendto(sd, buffer, recv_size, 0, (struct sockaddr*)&pos->addr, sizeof(struct sockaddr_un));
            printf("sendto size: %d , %s \n", send_size, pos->addr.sun_path);

            if(send_size<0){
                list_del(&pos->head);
                free(pos);
            }
        }
        printf("client list len %d\n", len);
    }
    
    return 0;
}
