//============================================================================
// Author      : Adney Cardoza
// Version     : 1.0
//============================================================================

#include <stdio.h>
#include <cstdlib>
#include <unistd.h>
#include <pthread.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/tcp.h>

#include <stdarg.h>


#include <string.h>
#include <errno.h>

#define SERVER_TIMEOUT 60  // in seconds
#define DEFAULT_TEST_DURATION 30  // in seconds


// Multi-flow Server that handles multiple clients running inside adv link shell
// Each client is a seperate flow
// Once client is connected, server will continuously send data


static int test_ongoing = 1;
bool send_traffic = true;

class FlowInfo
{
public:
    FlowInfo();
    FlowInfo(int sock, int flowid, int datasize);
public:
    int sock;
    int flowid;
    int datasize;
};

FlowInfo::FlowInfo()
{
    sock=-1;
    flowid=-1;
}

FlowInfo::FlowInfo(int sock, int flowid, int datasize)
{
    sock=sock;
    flowid=flowid;
    datasize=datasize;
}


uint64_t raw_timestamp( void )
{
    struct timespec ts;
    clock_gettime( CLOCK_REALTIME, &ts );
    uint64_t us = ts.tv_nsec / 1000;
    us += (uint64_t)ts.tv_sec * 1000000;
    return us;
}
uint64_t timestamp_begin(bool set)
{
        static uint64_t start;
        if(set)
            start = raw_timestamp();
        return start;
}
uint64_t timestamp_end( void )
{
        return raw_timestamp() - timestamp_begin(0);
}

uint64_t initial_timestamp( void )
{
        static uint64_t initial_value = raw_timestamp();
        return initial_value;
}

uint64_t timestamp( void )
{
        return raw_timestamp() - initial_timestamp();
}


void usage()
{
    printf("test_advlinkshell: ./test_advlinkshell <path> <num_flows> <port_base> <data size in KB> [optional: test_duration (seconds)]\n");
}

void inform( const char* format, ... )
{
    va_list args;
    printf("test_advlinkshell: ");
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}


void error( const char* format, ... )
{
    va_list args;
    printf("test_advlinkshell: ERROR: ");
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}



void* flow_data_handler(void* info)
{
    FlowInfo *flow = (FlowInfo*)info;
    int sock_local = flow->sock;
    int flowid = flow->flowid;
    int datasize = flow->datasize;
    char data_chunk[datasize+1];
    memset(data_chunk, 1, datasize);
    data_chunk[datasize]='\0';

    inform("sending %d KB to client %d", datasize, flowid);
    
    int len, start_len, sent;
    sent = 0;
    int send_counts = 0;
    while (send_traffic)
    {
        len = strlen(data_chunk);
        start_len = len;
        while(len>0)
        {
            sent = send(sock_local, data_chunk, strlen(data_chunk), 0);
            len -= sent;
            //inform("sent %d of %d bytes to client %d", sent, start_len, flowid);
            usleep(50);
        }
        send_counts++;
        //inform("sent %d KB to client %d, %d times", start_len, flowid, send_counts);
        usleep(100);
    }
    inform("finished sending data chunk of size: %d KB", datasize);
    close(sock_local);
    return (void*)0;
}

void* test_timer_handler(void* info)
{
    int duration = *((int*) info);
    uint64_t start = timestamp();
    uint64_t elapsed;
    if (duration != 0)
    {
        while (send_traffic)
        {
            sleep(1);
            elapsed = ((timestamp() - start)/1000000); // seconds
            if (elapsed > duration)
            {
                send_traffic = false;
            }
        }
    }
    return (void*) 0;
}

int main( int argc, char *argv[] ) 
{
    if (argc < 5)
    {
        usage();
        return 0;
    }

    char* path = argv[1];
    const int num_flows = atoi(argv[2]);
    int port_base = atoi(argv[3]);
    int datasize = atoi(argv[4]);

    int test_duration = DEFAULT_TEST_DURATION;
    if (argc == 6)
    {
        test_duration = atoi(argv[5]);
    }
    FlowInfo *flows;

    pthread_t client_data_thread[num_flows];

    pthread_t test_timer_thread;

    flows = new FlowInfo[num_flows];
    if (flows == NULL)
    {
        error("flow info init failed");
        return 0;
    }
    
    // for orca server make client_cnt_threads TODO (later)
    
    struct sockaddr_in server_addr[num_flows];
    struct sockaddr_in client_addr[num_flows];
    int sock[num_flows];

    int flow_index = 0;

    int i;
    for (i = 0; i < num_flows; i++)
    {
        memset(&server_addr[i], 0, sizeof(server_addr[i]));
        server_addr[i].sin_family=AF_INET;
        server_addr[i].sin_addr.s_addr=INADDR_ANY;
        server_addr[i].sin_port=htons(port_base+i);

        if ((sock[i] = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        {
            printf("test_advlinkshell: socket[%d] error: %s\n", i, strerror(errno));
            return 0;
        }
        inform("SUCCESS: created sockets");
        
        int reuse = 1;
        if (setsockopt(sock[i], SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0)
        {
            printf("test_advlinkshell: socket[%d] setsockopt REUSE error: %s\n", i, strerror(errno));
            return 0;
        }
        inform("SUCCESS: REUSEADDR set for sockets");

        if (bind(sock[i], (struct sockaddr*) &server_addr[i], sizeof(struct sockaddr)) < 0)
        {
            printf("test_advlinkshell: socket[%d] bind error: %s\n", i, strerror(errno));
            close(sock[i]);
            return 0;
        }
        inform("SUCCESS: bound sockets");
        // Add CC Scheme related code here TODO
    }

    char client_commands[num_flows][1500];

    for (i = 0; i < num_flows; i++)
    {
        char container_cmd[500];
        sprintf(container_cmd, "%s/test_advlinkshell_client $MAHIMAHI_BASE %d %d", path, port_base+i, i);
        
        // make log paths here
        char downlink_advlogpath[500];
        sprintf(downlink_advlogpath, "%s/log_advlink_advdownlink_%d.csv", path, i);
        char downlink_logpath[500];
        sprintf(downlink_logpath, "%s/log_advlink_downlink_%d.tr", path, i);


        sprintf(client_commands[i], "mm-advlink %s/../../traces/wired6 %s/../../traces/wired6 --downlink-advlog=%s --downlink-log=%s -- sh -c \'%s\' &", path, path, downlink_advlogpath, downlink_logpath, container_cmd);

        inform("created client command for flow %d: %s", i, client_commands[i]);
    }

    // start clients in containers
    for (i = 0; i < num_flows; i++)
    {
        system(client_commands[i]);
    }


    // start server listen here
    inform("starting server listen for %d flows", num_flows);
    int maxfdp = -1;
    fd_set rset;
    FD_ZERO(&rset);

    for (i = 0; i < num_flows; i++)
    {
        listen(sock[i], 1);
    }



    int sin_size = sizeof(struct sockaddr_in);

    // start test timer thread here
    if (pthread_create(&test_timer_thread, NULL, test_timer_handler, (void*)&test_duration) < 0)
    {
        error("couldn't create test timer thread");
        return 0;
    }

    initial_timestamp();
    while(flow_index < num_flows)
    {
        FD_ZERO(&rset);
        for (i = 0; i < num_flows; i++)
        {
            FD_SET(sock[i], &rset);
            if (sock[i] > maxfdp)
                maxfdp = sock[i];
        }
        maxfdp=maxfdp + 1;
        struct timeval timeout;
        timeout.tv_sec = SERVER_TIMEOUT;
        timeout.tv_usec = 0;
        int rc = select(maxfdp, &rset, NULL, NULL, &timeout);

        if (rc < 0)
        {
            error("select failed");
            return 0;
        }

        if (rc == 0)
        {
            error("server timed out");
            return 0;
        }
        
        inform("waiting on flow index: %d", flow_index);
        if (FD_ISSET(sock[flow_index], &rset))
        {
            inform("trying to accept connection for flow %d", flow_index);
            int value = accept(sock[flow_index], (struct sockaddr*)&client_addr[flow_index], (socklen_t*)&sin_size);
            if (value < 0)
            {
                error("flow %d accept error: %s", flow_index, strerror(errno));
                close(sock[flow_index]);
                return 0;
            }

            inform("trying to create data thread for flow %d, sock[%d]: %d with accepted socket: %d", flow_index, flow_index, sock[flow_index], value);
            // create data thread for this flow here
            flows[flow_index].flowid = flow_index;
            flows[flow_index].sock = value;
            flows[flow_index].datasize = datasize;
            if (pthread_create(&client_data_thread[flow_index], NULL, flow_data_handler, (void*)&flows[flow_index]) < 0)
            {
                error("couldn't create data thread for flow %d");
                close(sock[flow_index]);
                return 0;
            }

            inform("SUCCESS: Client of flow %d has connected to server", flow_index);
            flow_index++;
        }
    }
    // join all data threads here
    for (i = 0; i < num_flows; i++)
    {
        pthread_join(client_data_thread[i], NULL);
    }
    inform("SUCCESS: test finished.");
    return 0;
}
