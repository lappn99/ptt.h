#include <stdio.h>
#define PTTH_IMPLEMENTATION
#include "../ptt.h"

int main(int argc, char** argv)
{
    
    int result = ptthInit((PtthInitDesc){
            .port = 8080
        }
    );
    if(result == 0)
    {
        fprintf(stderr,"Could not start PTTH");
    }

    do
    {
        ptthProcess();
    }while(ptthContinue());

    ptthDeinit();
}
