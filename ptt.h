#ifndef _PTT_H
#define _PTT_H

#include <stddef.h>
struct HttpServer;
typedef struct HttpServer* HttpServerHandle;

typedef enum
{
    HTTP_GET,
    HTTP_POST
} HttpRequestMethod;

typedef enum
{
    HTTP_1_1,
    HTTP_2_0
} HttpVersion;



typedef struct 
{
    HttpRequestMethod method;
    char resource[2048];
    HttpVersion version;
    

} HttpRequest;

typedef struct
{
    unsigned int code;
    HttpVersion version;
    const char* content;
    size_t contentLength;
} HttpResponse;

 
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
#include <assert.h>
#include <errno.h> //errno
#include <fcntl.h> //fcntl
#include <netdb.h>
#include <signal.h> //signal
#include <stdarg.h> //va_start, va_list
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
static HttpRequest parseRequest(const char*);
static void sendResponse(struct HttpConnection*, HttpResponse);
static size_t getContent(const char*, char*);

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
            len = recv(connection->socket,data,sizeof(data),0);
            
                
            if(len == 0)
            {
               errno = 0;
               closeConnection(connection);
            
            }
            else
            {
                HttpRequest request = parseRequest(data);
                fprintf(stdout,"Resource: %s\n",request.resource);
                if(request.method == HTTP_GET)
                {
                    fprintf(stdout,"GET Request\n");
                    char* content = NULL;
                    size_t contentLength = getContent(request.resource,content);
                    sendResponse(connection,(HttpResponse){
                        .code = 200,
                        .version = request.version,
                        .content = content,
                        .contentLength = contentLength
                    });
                }
                if(request.version == HTTP_1_1)
                {
                    fprintf(stdout,"Using HTTP/1.1\n");
                }
                

                
                closeConnection(connection);

            }
            //fprintf(stdout,"%s\n",data);
            
            if(errno != 0 && errno != EAGAIN )
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

static HttpRequest
parseRequest(const char* data)
{
    HttpRequest request;
    request.version = HTTP_1_1;
    char* requestDup = strdup(data);
    char* tokLast;
    char* requestLine;
    char method[5], version[16];

    requestLine = strtok_r(requestDup,"\r\n",&tokLast);

    fprintf(stderr,"%s\n",requestLine);
    sscanf(requestLine,"%4s %2047s %15s",method,request.resource,version);
    fprintf(stderr,"%s\n",request.resource);
    if(strcmp(method,"GET") == 0)
    {
        request.method = HTTP_GET;
    } 
    else if(strcmp(method,"POST") == 0)
    {
        request.method = HTTP_POST;
    }
    else
    {
        fprintf(stderr,"Unknown http method: %s",method);
    }

    if(strcmp(version,"HTTP/1.1") == 0)
    {
        request.version = HTTP_1_1;
    }
    else
    {
        fprintf(stderr,"Unknown or unsupported HTTP version: %s",version);
    }

    free(requestDup);
    return request;
    
}

static void 
sendResponse(struct HttpConnection* connection, HttpResponse response)
{
    const char htmlString[] = "<h1>PTTH!</h1>";

    char* responseLine;
    int responseLen = snprintf(NULL,0,"HTTP/1.1 %d OK\r\nConnection:close\r\nContent-Type:text/html\r\n\r\n",response.code);
    if(responseLen < 0)
    {
        perror("snprintf()");
        return;
    }
    responseLine = malloc((responseLen + 1) * sizeof(char));
    responseLen = snprintf(responseLine,responseLen + 1,"HTTP/1.1 %d OK\r\nConnection:close\r\nContent-Type:text/html\r\n\r\n",response.code);
    fprintf(stderr,"Sending: %s",responseLine);
    //int result = sendto(server.socket,responseLine,responseLen,0,(struct sockaddr*)&connection->address,sizeof(connection->address));
    int result = send(connection->socket,responseLine,responseLen, 0);
    if(result < 0)
    {
        perror("sendto");
    }
    send(connection->socket,htmlString,sizeof(htmlString),0);
    free(responseLine);
}

static size_t 
getContent(const char* name, char* content)
{
    assert(0);
}


#endif // PTTH_IMPLEMENTATION

#endif // _PTT_H


