#include <stdio.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/mman.h>

#ifndef _POSIX_THREAD_PROCESS_SHARED
#error _POSIX_THREAD_PROCESS_SHARED
#endif

#define K 1024
#define M (1024*1024)
#define MMAP_SIZE (2*M)

char *strupper(char *s){
    char *cur = s;
    while(*cur != '\0'){
        *cur = toupper(*cur);
        cur ++;
    }
    return s;
}

struct mem_string_list {
    pthread_mutexattr_t mutex_attr;
    pthread_mutex_t mutex;
    pthread_condattr_t cond_attr;
    pthread_cond_t cond;
    
    bool running;

    char *mem_end;
    size_t str_num;
    char *payload_end;
    char payload[0];
};

typedef struct mem_string_list mslist_t;

void mslist_init(mslist_t *mdata, void *addr){
    pthread_mutexattr_init(&mdata->mutex_attr);
    pthread_mutexattr_setpshared(&mdata->mutex_attr, PTHREAD_PROCESS_SHARED); //支持多进程
    pthread_mutex_init(&mdata->mutex, &mdata->mutex_attr);

    pthread_condattr_init(&mdata->cond_attr);
    pthread_condattr_setpshared(&mdata->cond_attr, PTHREAD_PROCESS_SHARED); //支持多进程
    pthread_cond_init(&mdata->cond, &mdata->cond_attr);

    mdata->running = true;

    mdata->mem_end = addr + MMAP_SIZE;
    // mdata->maxlen = (char*)mdata->mem_end - (char*)mdata->payload;
    
    mdata->str_num = 0;
    mdata->payload_end = mdata->payload;
}

void mslist_pushback_wait(mslist_t *mdata, const char *s){
    assert(s != NULL);

    size_t slen = strlen(s);
    if(slen == 0){
        return;
    }

    pthread_cond_signal(&mdata->cond);
    pthread_mutex_lock(&mdata->mutex);
    
    while((mdata->mem_end - mdata->payload_end) <= slen){
        pthread_cond_wait(&mdata->cond, &mdata->mutex);
    }
    
    // copy string
    char *pos = mdata->payload_end;
    while(*pos++ = *s++){}
    mdata->payload_end = pos;
    mdata->str_num += 1;

    pthread_mutex_unlock(&mdata->mutex);
}

void mslist_pop_wait(mslist_t *mdata, char *dst, size_t len){
    assert(dst != NULL);

    size_t dlen = strlen(dst);
    pthread_mutex_lock(&mdata->mutex);
    
    while(mdata->str_num < 1){
        pthread_cond_wait(&mdata->cond, &mdata->mutex);
    }
    
    // find string
    char *s = mdata->payload_end - 2; // payload_end - 1 是 '\0'
    while(*s != '\0' && s >= mdata->payload){
        s--;
    }
    s++; // str start

    strncpy(dst, s, len);
    mdata->payload_end = s;
    mdata->str_num--;

    pthread_mutex_unlock(&mdata->mutex);
}


int main(void){

    const char* send_list[] = {
        "beijing",
        "shanghai",
        "guangzhou",
        "shenzhen",
        "nanjing",
        "wuhan",
        "hangzhou",
        NULL
    };


    void *addr = mmap(NULL, MMAP_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    mslist_t *mdata = addr;
    mslist_init(mdata, addr);

    pid_t pid = fork();
    
    if (pid > 0){
        //parent

        mslist_pushback_wait(mdata, "hello");
        fprintf(stderr, "parent push %s\n", "hello");
        
        mslist_pushback_wait(mdata, "world");
        fprintf(stderr, "parent push %s\n", "world");


        for(int i=0; send_list[i] != NULL; i++){
            mslist_pushback_wait(mdata, send_list[i]);
            fprintf(stderr, "parent push %s\n", send_list[i]);
            sleep(1);
        }

        wait(NULL);
        fprintf(stderr, "parent done\n");


    }else if(pid == 0){
        // _exit(0);
        //child
        char buf[1024];

        while(true){
            mslist_pop_wait(mdata, buf, 1024);
            strupper(buf);        
            fprintf(stderr, "child: %s\n", buf);
        }

        fprintf(stderr, "child exit\n");

    }else{
        //error
    }

    munmap(addr, MMAP_SIZE);
    return 0;
}
