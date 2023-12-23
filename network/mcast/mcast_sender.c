#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <arpa/inet.h>

//组播可以使用UDP或者原始IP实现，不支持UDP

#define DESTADDR  "230.0.0.5"
#define DESTPORT  12345

int main()
{
	char buf[] = "hello";

	struct sockaddr_in dest = {};
    int sd = socket(AF_INET, SOCK_DGRAM, 0);

	dest.sin_family = AF_INET;
	dest.sin_addr.s_addr = inet_addr(DESTADDR);
	dest.sin_port = htons(DESTPORT);
	
	while(true) {
		sendto(sd, buf, sizeof(buf), 0, (struct sockaddr *)&dest, sizeof(dest));
		sleep(1);
	}

	close(sd);
    return 0;
}
