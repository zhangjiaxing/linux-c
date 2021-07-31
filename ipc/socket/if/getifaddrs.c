#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <assert.h>
#include <netpacket/packet.h>
#include <netinet/ether.h>

const char *sockaddr_to_ipstr(const struct sockaddr *addr, char *cstr, size_t len){
    assert(len > 1);
    void *addr_ptr = NULL;

    if(addr == NULL){
        *cstr = '\0';
        return cstr;
    }
    
    switch(addr->sa_family){
    case AF_INET:
        addr_ptr = &((struct sockaddr_in *)  addr)->sin_addr;
        return inet_ntop(addr->sa_family, addr_ptr, cstr, len);
        break;
    case AF_INET6:
        addr_ptr = &((struct sockaddr_in6 *) addr)->sin6_addr;
        return inet_ntop(addr->sa_family, addr_ptr, cstr, len);
        break;
    case AF_PACKET:
        addr_ptr = &((struct sockaddr_ll *) addr)->sll_addr;
        return ether_ntoa_r(addr_ptr, cstr);
        break;
    default:
        break;
    }
    return NULL;
}

int main(){
    struct ifaddrs *addrs;
    getifaddrs(&addrs);

    char addr_cstr[INET6_ADDRSTRLEN];
    char netmask_cstr[INET6_ADDRSTRLEN];
    char broadaddr_cstr[INET6_ADDRSTRLEN];
    char dstaddr_cstr[INET6_ADDRSTRLEN];

    for(struct ifaddrs *cur=addrs; cur!=NULL; cur=cur->ifa_next){
        sa_family_t family = cur->ifa_addr->sa_family;
        if(family == AF_PACKET){
            sockaddr_to_ipstr(cur->ifa_addr, addr_cstr, INET6_ADDRSTRLEN);
            printf("%s :\nMAC: [%s]\n\n", cur->ifa_name, addr_cstr);
        }else{
            sockaddr_to_ipstr(cur->ifa_addr, addr_cstr, INET6_ADDRSTRLEN);
            sockaddr_to_ipstr(cur->ifa_netmask, netmask_cstr, INET6_ADDRSTRLEN);
            sockaddr_to_ipstr(cur->ifa_broadaddr, broadaddr_cstr, INET6_ADDRSTRLEN);
            sockaddr_to_ipstr(cur->ifa_dstaddr, dstaddr_cstr, INET6_ADDRSTRLEN);
        
            printf("%s :\naddr: [%s] netmask: [%s] ", 
                    cur->ifa_name,
                    addr_cstr, 
                    netmask_cstr
            );
    
            if(cur->ifa_flags | IFF_BROADCAST && family == AF_INET){
                printf("broad: [%s] \n\n", broadaddr_cstr);
            }else if(cur->ifa_flags | IFF_POINTOPOINT && family == AF_INET) {
                printf("dst: [%s] \n\n", dstaddr_cstr);
            }else{
                // IFF_LOOPBACK...
                printf("\n\n");
            }
        }
    }

    freeifaddrs(addrs);
    return 0;
}
