#ifndef _PTT_H
#define _PTT_H

#include <stddef.h>

struct HttpServer;
typedef struct HttpServer* HttpServerHandle;
struct HttpConnection;
typedef struct HttpConnection* HttpConnectionHandle;

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
    const char* content;
    size_t contentLength;
} HttpResponse;

typedef struct
{
    HttpRequestMethod method;
    const char* endPoint;
    HttpResponse (*routeTarget)(HttpConnectionHandle, HttpRequest);

} PtthRoute;

typedef struct
{
    int port;
    const char* serverBaseDir;
    size_t numRoutes;
    PtthRoute* routes;

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
#include <limits.h> //PATH_MAX
#include <netdb.h>
#include <signal.h> //signal
#include <stdarg.h> //va_start, va_list
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h> //exit
#include <string.h> // strerror
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h> //socket
#include <sys/types.h>
#include <unistd.h> //close, fork, getcwd

#ifndef PTTH_CONN_BACKLOG
#define PTTH_CONN_BACKLOG 10
#endif // PTTH_CONN_BACKLOG

#ifndef PTTH_MAX_ROUTES
#define PTTH_MAX_ROUTES 24
#endif


struct HttpServer 
{
    int socket;   
    struct sockaddr_in address;
    int epoll;
    int shutdown;
    int baseDir;

    PtthRoute routes[PTTH_MAX_ROUTES]; 
    size_t numRoutes;
    
};

struct HttpConnection
{
    int socket;
    struct sockaddr_in address;
    HttpVersion version;
};

static void acceptNewClient(void);
static void closeConnection(struct HttpConnection*);
static HttpRequest parseRequest(const char*);
static void sendResponse(struct HttpConnection*, HttpResponse);
static size_t getContent(const char*, char**);

#ifndef PTTH_NO_SIGHANDLE
static void ptthSignalHandler(int);
#endif // PTTH_NO_SIGHANDLE
static struct HttpServer server;

const char RESPONSE_404[] = "<h1>File not found</h1>";

static int
ptthInit(PtthInitDesc desc)
{
    if((server.socket = socket(AF_INET,SOCK_STREAM,0)) < 0)
    {
        perror("socket()");
        return 0;
    }
    
    if(setsockopt(server.socket,SOL_SOCKET,SO_REUSEADDR,&(int){1},sizeof(int)) < 0)
    {
        perror("setsockopt()");
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

    char baseDirPath[PATH_MAX];
    getcwd(baseDirPath,PATH_MAX);
    if(desc.serverBaseDir != NULL)
    {
        strncpy(baseDirPath,desc.serverBaseDir,PATH_MAX);
    } 
    server.baseDir = open(baseDirPath,O_DIRECTORY);
    if(server.baseDir < 0)
    {
        perror("open()");
    }

    memcpy(server.routes,desc.routes,sizeof(PtthRoute) * desc.numRoutes);
    server.numRoutes = desc.numRoutes;

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
    close(server.baseDir);
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
                connection->version = request.version;
                int routeHandled = 0;
                for(int i = 0;i < server.numRoutes; i++)
                {
                    PtthRoute route = server.routes[i];
                    if(strcmp(route.endPoint, request.resource) == 0 && route.method == request.method)
                    {
                        fprintf(stderr,"Routing request %s\n", request.resource);
                        sendResponse(connection, route.routeTarget(connection,request));

                        routeHandled = 1;
                                 
                    }
                }
                if(request.method == HTTP_GET && !routeHandled)
                {
                    fprintf(stdout,"GET Request\n");
                    char* content = NULL;
                    size_t contentLength = getContent(request.resource,&content);
                    int responseCode = 200;
                    if(content == NULL)
                    {
                        responseCode = 404;                     
                        content = strdup(RESPONSE_404);
                        contentLength = sizeof(RESPONSE_404);

                    }
                    sendResponse(connection,(HttpResponse){
                        .code = responseCode,
                        .content = content,
                        .contentLength = contentLength
                    });
                    free(content);
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
    shutdown(server.socket,SHUT_RDWR);
    if(close(server.socket) < 0)
    {
        perror("close()");
    }

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
    if(strcmp(request.resource,"/") == 0)
    {
        strcpy(request.resource,"/index.html");
    }
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

    char* responseLine;
    char reasonPhrase[32];
    const char responseFormat[] = "HTTP/1.1 %d %s\r\nConnection:close\r\nContent-Type:text/html\r\n\r\n";

    switch(response.code)
    {
        case 200:
            strcpy(reasonPhrase,"OK");
            break;
        case 404:
            strcpy(reasonPhrase,"File not Found");
            break;
        default:
            fprintf(stderr,"Unknown or unsupported response code: %d", response.code);
            break;
    }

    int responseLen = snprintf(NULL,0,responseFormat,response.code,reasonPhrase);
    if(responseLen < 0)
    {
        perror("snprintf()");
        return;
    }
    responseLine = malloc((responseLen + 1) * sizeof(char));
    responseLen = snprintf(responseLine,responseLen + 1,responseFormat,response.code,reasonPhrase);
    
    int result = send(connection->socket,responseLine,responseLen, 0);
    if(result < 0)
    {
        perror("sendto");
    }
    send(connection->socket,response.content,response.contentLength,0);
    free(responseLine);
}

static size_t 
getContent(const char* name, char** content)
{
    char* last;
    char* path = strdup(name);
    
    char* currentPath = strtok_r(path,"/",&last);
    int currentDir = server.baseDir;
next_dir:
    int file = openat(currentDir, currentPath, O_RDONLY);
    if(file == -1)
    {
        perror("openat()");
        goto get_content_error;
    }

    printf("Path: %s\n",path); 
    struct stat fileStat;
    fstat(file, &fileStat);

    if(S_ISDIR(fileStat.st_mode))
    {
        if(currentDir != server.baseDir)
        {
            close(currentDir);
        }
        currentPath = strtok_r(NULL,"/",&last); 
        currentDir = file;
        goto next_dir;
    }
    
    if(currentDir != server.baseDir)
    {
        close(currentDir);
    }

    size_t contentSize;
    off_t size = lseek(file,0,SEEK_END);
    *content = malloc(sizeof(char) * (size + 1));
    lseek(file,0,SEEK_SET);
    if((contentSize = read(file, *content, size)) < 0)
    {
        perror("read()");
        goto get_content_error;
    }
    fprintf(stderr,"Resource size: %lu\n",contentSize);
    free(path);    
    close(file);
    return size;
get_content_error:
    errno = 0;
    free(path);
    return 0;
   
}


#endif // PTTH_IMPLEMENTATION

#endif // _PTT_H


