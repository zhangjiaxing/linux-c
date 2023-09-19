#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/ip.h>

/**************
 * 原始套接字包括 数据链路层和ip层， 这里是ip层的raw socket
 * 参考 man 7 packet 和 man 7 raw
 **************/

#define SELF_IP "192.168.7.109"
#define REMOTE_IP "1.2.3.4"
#define BUFLEN 1024

#define ERR_EXIT(msg)   do {    \
        char buf[1024] = {};    \
        snprintf(buf, 1024, "%d: %s: %s\n", __LINE__, __FUNCTION__, msg); \
        perror(msg);    \
        exit(EXIT_FAILURE);        \
    } while(0)



int main() {
    int ret = 0;
    int raw_socket = socket(AF_INET, SOCK_RAW, IPPROTO_ESP);
    if(raw_socket < 0) {
        ERR_EXIT("socket");
    }

    /* bind: 只接受指定地址和协议的包 */
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(IPPROTO_ESP);
    addr.sin_addr.s_addr = inet_addr(SELF_IP);
    if(bind(raw_socket, (void*)&addr, sizeof(addr)) < 0) {
        ERR_EXIT("bind");
    }

    /* 将此套接字绑定到一个特定的设备上 */
    char *ifdev = "wlp2s0";
    ret = setsockopt(raw_socket, SOL_SOCKET, SO_BINDTODEVICE, ifdev, sizeof("wlp2s0"));
    if(ret < 0) {
        ERR_EXIT("SO_BINDTODEVICE");
    }

    addr.sin_addr.s_addr = inet_addr(REMOTE_IP);
    const char *msg = "hello";
    char buf[BUFLEN] = {};
    struct ip_esp_hdr *hdr = (void*)buf;
    hdr->spi = htonl(0x12345678);
    hdr->seq_no = htonl(1000);
    strcpy(hdr->enc_data, msg);

    ssize_t nbytes = sendto(raw_socket, buf, sizeof(struct ip_esp_hdr) + sizeof("hello"), 0, (void*)&addr, sizeof(addr));
    if(nbytes < 0) {
        ERR_EXIT("sendto");
    }

    return 0;
}

