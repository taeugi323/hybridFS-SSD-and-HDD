#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include<pthread.h>

FILE *fp;
int seekpoint = 0;
void *thread_func();

int main()
{
    int i;
    pthread_t threads[10];

    fp = fopen("large_file.txt","r+");

    for (i=0;i<10;i++) 
        pthread_create(&(threads[i]), NULL, thread_func, 0);

    for (i=0;i<10;i++) 
        pthread_join(threads[i], NULL);

    return 0;
}

void* thread_func() 
{
    int delta, i;

    srand(time(NULL));

    for (i=0;i<10;i++) {
        delta = rand()%100 + 5;

        fseek(fp, delta, SEEK_CUR);
        fprintf(fp, "hello");
    }
}
