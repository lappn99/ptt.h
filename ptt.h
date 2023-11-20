#ifndef _PTT_H
#define _PTT_H

struct HttpServer;
typedef struct HttpServer* HttpServerHandle;

typedef struct 
{

} PtthResponse;

typedef struct 
{
    
} PtthRequest;


typedef PtthResponse (*HandleRequest)(PtthRequest);
 

typedef struct
{
    int port;
} PtthInitDesc;


static int ptthInit(PtthInitDesc);
static void ptthDeinit(void);
static int ptthContinue(void);
static void ptthProcess(void);


#ifdef PTTH_IMPLEMENTATION

#include <arpa/inet.h> //htons
#include <errno.h> //errno
#include <fcntl.h> //fcntl
#include <netdb.h>
#include <signal.h> //signal
#include <stdarg.h> //va_start, va_list
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h> //exit
#include <string.h> // strerror
#include  <sys/epoll.h>
#include <sys/socket.h> //socket
#include <sys/types.h>
#include <unistd.h> //close, fork

#ifndef PTTH_CONN_BACKLOG
#define PTTH_CONN_BACKLOG 10
#endif // PTTH_CONN_BACKLOG

struct HttpServer 
{
    int socket;   
    struct sockaddr_in address;
    int epoll;

    int shutdown;
    
};

struct HttpConnection
{
    int socket;
    struct sockaddr_in address;
};

static void acceptNewClient(void);
static void closeConnection(struct HttpConnection*);
#ifndef PTTH_NO_SIGHANDLE
static void ptthSignalHandler(int);
#endif // PTTH_NO_SIGHANDLE
static struct HttpServer server;


static int
ptthInit(PtthInitDesc desc)
{
    if((server.socket = socket(AF_INET,SOCK_STREAM,0)) < 0)
    {
        perror("socket()");
        return 0;
    }
    server.shutdown = 0;
    server.address.sin_family = AF_INET;
    server.address.sin_port = htons(desc.port);
    server.address.sin_addr.s_addr = INADDR_ANY;

    if(bind(server.socket,(const struct sockaddr*)&server.address,sizeof(server.address)) < 0)
    {
        perror("bind()");
        return 0;
    }

    if(listen(server.socket,PTTH_CONN_BACKLOG) != 0)
    {
        perror("listen()");
        return 0;
    }
    fprintf(stderr,"PTTH: Started server on port %d\n",desc.port);
    server.epoll = epoll_create1(0);
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLRDHUP;
    ev.data = (epoll_data_t){.fd = server.socket};
    epoll_ctl(server.epoll,EPOLL_CTL_ADD,server.socket,&ev);

    #ifndef PTTH_NO_SIGHANDLE
    signal(SIGINT,ptthSignalHandler);
    signal(SIGQUIT,ptthSignalHandler);
    #endif // PTTH_NO_SIGHANDLE


    return 1;


}

static void
ptthDeinit(void)
{
    close(server.socket);
}

static int
ptthContinue(void)
{
    return !server.shutdown;
}

static void
ptthProcess(void)
{
    #define MAX_EVENTS PTTH_CONN_BACKLOG + 1
    int i;
    struct epoll_event pendingEvents[MAX_EVENTS];
    int numEvents = epoll_wait(server.epoll,pendingEvents,MAX_EVENTS,-1);
    for(i = 0; i < numEvents; i++)
    {
        struct epoll_event event = pendingEvents[i];

        //Event on server socket
        if(event.data.fd == server.socket)
        {
            //Socket is written to (new client)
            if(event.events & EPOLLIN)
            {
                acceptNewClient();
            }
            
        }
        else if(event.events & EPOLLIN)
        {
            
            struct HttpConnection* connection = event.data.ptr;

            char data[2048];
            int len = 0;
            while((len = recv(connection->socket,data,sizeof(data),0)) < sizeof(data))
            {
                
                if(len == 0)
                {
                    errno = 0;
                    closeConnection(connection);
                    break;
                }

                fprintf(stdout,"%s\n",data);
            }
            if(errno != 0 )
            {
                perror("recv()");
               
            } 
            errno = 0;
            
        }
        else if(event.events & (EPOLLRDHUP | EPOLLERR))
        {
            server.shutdown = 1;
            fprintf(stderr,"Connection closing");
            struct HttpConnection* connection = event.data.ptr;
            closeConnection(connection);
            
        }

    }
}

static void
acceptNewClient(void)
{
    struct sockaddr_in client;
    unsigned int namelen = sizeof(client);

    int socket;
    struct HttpConnection* connection = malloc(sizeof(struct HttpConnection));

    if((socket = accept(server.socket, (struct sockaddr*)&client,&namelen)) == -1)
    {
        perror("accept()");
        return;
    } 

    if(fcntl(socket,F_SETFL,O_NONBLOCK) == -1)
    {
        perror("fcntl");
    }

    connection->address = client;
    connection->socket = socket;

    struct epoll_event ev;
    ev.data.ptr = connection;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;

    epoll_ctl(server.epoll,EPOLL_CTL_ADD,connection->socket,&ev);
    


}

static void 
closeConnection(struct HttpConnection* connection)
{
    struct epoll_event event;
    epoll_ctl(server.epoll,EPOLL_CTL_DEL,connection->socket,&event);
    close(connection->socket);
    free(connection);
}

static void 
ptthSignalHandler(int)
{
    fprintf(stderr,"Closing PTTH server\n");
    close(server.socket);
    server.shutdown = 1;
}


#endif // PTTH_IMPLEMENTATION

#endif // _PTT_H


