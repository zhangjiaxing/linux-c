#include <sys/socket.h>
#include <netinet/ip.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <stddef.h>


#define SOCKADDR_TO_IPV4STR(__sockaddr_p, __char_p, __buf_len) do{\
            struct in_addr * __addr = &((struct sockaddr_in *) __sockaddr_p)->sin_addr; \
            inet_ntop(AF_INET, __addr, __char_p, __buf_len); \
        }while(0)


int main(void){
	int sd = socket(AF_INET, SOCK_STREAM, 0);
	if(sd == -1){
		perror("socket");
		exit(EXIT_FAILURE);
	}

	struct ifconf ifc;
	memset(&ifc, 0, sizeof(ifc));
	if(ioctl(sd, SIOCGIFCONF, &ifc) == -1){
		perror("SIOCGIFCONF");
		exit(EXIT_FAILURE);
	}
	
	printf("共有%d个网络接口地址\n", ifc.ifc_len/sizeof(struct ifreq));
	
    ifc.ifc_buf = malloc(ifc.ifc_len);
    if(ioctl(sd, SIOCGIFCONF, &ifc) == -1){
        perror("SIOCGIFCONF");
        exit(EXIT_FAILURE);
    }
    
    int if_count = ifc.ifc_len/sizeof(struct ifreq);
    char ipaddr[INET_ADDRSTRLEN] = {0};
    struct ifreq *ifreq_p;

    for(int i=0; i<if_count; i++){
        ifreq_p = &ifc.ifc_req[i];

        struct in_addr *addr;
        addr = &((struct sockaddr_in *) &ifreq_p->ifr_addr)->sin_addr;
        inet_ntop(AF_INET, addr, ipaddr, INET_ADDRSTRLEN);
        SOCKADDR_TO_IPV4STR(&ifreq_p->ifr_addr, ipaddr, INET_ADDRSTRLEN);
        printf("%s %s\n", ifreq_p->ifr_name, ipaddr);
    }

    free(ifc.ifc_buf);
    ifc.ifc_buf = NULL;
    close(sd);
	
	return 0;
}
