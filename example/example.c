#include <stdio.h>
#define PTTH_IMPLEMENTATION
#include "../ptt.h"

const char* callbackResponse = "<h1>callback success!</h1>";
HttpResponse handleCallback(HttpConnectionHandle, HttpRequest);

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


HttpResponse handleCallback(HttpConnectionHandle, HttpRequest)
{
    //const char response[] = "test";
    printf("Handling /callback!\n");
    return (HttpResponse){
        .code = 200,
        .content = callbackResponse,
        .contentLength = strlen(callbackResponse)
    };
}
