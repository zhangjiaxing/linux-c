#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <arpa/inet.h>



#define MADDR	"230.0.0.5"
#define MPORT	12345
#define BUFSIZE		2000


/***
IP_ADD_MEMBERSHIP
struct ip_mreqn
{
	struct in_addr imr_multiaddr; // IP多点传送组地址
	struct in_addr imr_address;   //本地接口的IP地址
	int imr_ifindex; //接口索引
};
imr_multiaddr包含应用程序希望加入或者退出的多点广播组的地址.它必须是一个有效的多点广播地址.imr_address
指的是系统用来加入多点广播组的本地接口地址;如果它与  INADDR_ANY 一致,那么由系统选择一个合适的接口. imr_ifindex 指的是要加入/脱离
imr_multiaddr 组的接口索引,或者设为0表示任何接口.
**************/

int main()
{
	char buf[BUFSIZE];

	struct sockaddr_in maddr = {};
	struct ip_mreqn mreqn = {};
    int sd = socket(AF_INET, SOCK_DGRAM, 0);

	maddr.sin_family = AF_INET;
	maddr.sin_addr.s_addr = inet_addr(MADDR);
	maddr.sin_port = htons(MPORT);

	if (bind(sd, (struct sockaddr *)&maddr, sizeof(maddr)) != 0) {
		fprintf(stderr, "bind err\n");
	}

	mreqn.imr_multiaddr.s_addr = inet_addr(MADDR);
	mreqn.imr_address.s_addr = INADDR_ANY;
	mreqn.imr_ifindex = 0;

	if (setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreqn, sizeof(mreqn)) != 0) {
		fprintf(stderr, "setsockopt err\n");
	}

	while(true) {
		socklen_t len = 0;
		struct sockaddr_in from = {};
		memset(buf, 0, sizeof(buf));
		recvfrom(sd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &len);
		printf("received: [%s]\n", buf);
		fflush(stdout);
	}

	close(sd);
    return 0;
}
