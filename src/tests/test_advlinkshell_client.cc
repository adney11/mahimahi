#include <stdio.h>
#include <stdlib.h>
#include <cstdarg>
#include <string.h>
#include <netinet/in.h>  
#include <arpa/inet.h> 
#include <netinet/tcp.h>
#include <errno.h>
#include <unistd.h>

void usage()
{
    printf("./test_advlinkshell_client $MAHIMAHI_BASE <port> <flowid>\n");
}


int main( int argc, char* argv[])
{
    if (argc != 4)
    {
        usage();
        return 0;
    }

    char *server = (char*)malloc(16*sizeof(char));
    int port;
    struct sockaddr_in servaddr;
    int connected=0;
    int flowid;
    char buf[BUFSIZ];
    if (!server)
    {
        printf("client: couldn't get memory for server string\n");
        return 0;
    }
    memset(server, '\0', 16);
    server=argv[1];
    port=atoi(argv[2]);
    flowid = atoi(argv[3]);

    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family=AF_INET;
    servaddr.sin_addr.s_addr=inet_addr(server);
    servaddr.sin_port=htons(port);

    int sockfd;
    
    if((sockfd=socket(PF_INET,SOCK_STREAM,0))<0)
	{
		printf("client %d: socket error %s\n",flowid,  strerror(errno));
		return 0;
	}
	int reuse = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &reuse, sizeof(reuse)) < 0)
	{
		printf("client %d: TCP_NODELAY error: %s\n", flowid, strerror(errno));
		close(sockfd);
		return 0;
	}

    int i;
    for(i=0;i<120;i++)
    {
        if(connect(sockfd,(struct sockaddr *)&servaddr,sizeof(struct sockaddr))<0)
        {
            printf("client %d: Trying to connect to %s on port %d\n",flowid, server, port);
            usleep(500000);
        }
        else
        {
            connected=1;
            printf("client %d: Connected!\n", flowid);
            break;
        }
    }
    if(!connected)
    {
        printf("client %d: No success after 120 tries..\n", flowid);
        close(sockfd);
        return 0;
    }

    int len;
    while(1)
    {
        len=recv(sockfd, buf, BUFSIZ, 0);
        if (len <= 0)
        {
            printf("client %d: recv returned negative\n", flowid);
            break;
        }
    }
    close(sockfd);
    printf("client %d: done\n", flowid);
    return 0;
}