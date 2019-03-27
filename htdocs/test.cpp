#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main()
{
    int pid = 0;

    printf("cgi"); //较上个示例程序仅仅加了一个\n

    pid = fork();

    if (pid == 0) 
    {
        exit(0);
    } 
    else if (pid > 0) 
    {
        int status = 0;
        wait(&status);
    }

    return 0;
}