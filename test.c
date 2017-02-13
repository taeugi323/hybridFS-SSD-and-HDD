#include<stdio.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/types.h>

int main()
{
    int a = O_APPEND;
    printf("%d\n",a);
}
