#include <stdio.h>
#include <httpd.h>

int main(int argc, char **argv)
{
    Httpd httpd;
    LOG("startup port:%d", 8080);
    
    httpd.startup(8080);

    return 0;
}