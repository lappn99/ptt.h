#include <stdio.h>
#define PTTH_IMPLEMENTATION
#include "../ptt.h"

const char* callbackResponse = "<h1>callback success!</h1>";
HttpResponse handleCallback(HttpConnectionHandle connection, HttpRequest* request);

int main(int argc, char** argv)
{
    PtthRoute routes[] = {
        {.method = HTTP_GET, .endPoint = "/callback",.routeTarget= handleCallback}

    };
    int result = ptthInit((PtthInitDesc){
            .port = 8080,
            .numRoutes = 1,
            .routes = routes
        }
    );
    if(result == 0)
    {
        fprintf(stderr,"Could not start PTTH");
        exit(1);
    }

    do
    {
        ptthProcess();
    }while(ptthContinue());

    ptthDeinit();
}


HttpResponse handleCallback(HttpConnectionHandle connection, HttpRequest* request)
{
    
    //const char response[] = "test";
    printf("Handling /callback!\nQuery string: %s\n",request->data.queryString);
    return (HttpResponse){
        .code = 200,
        .content = callbackResponse,
        .contentLength = strlen(callbackResponse)
    };
}
