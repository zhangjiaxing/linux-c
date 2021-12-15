#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sched.h>

#include "list.h"

#ifndef NDEBUG
#define DEBUG_LOG(fmt, ...) do { fprintf(stderr, "%s: " fmt, __func__, ## __VA_ARGS__); } while (0)
#else
#define DEBUG_LOG(fmt, ...)
#endif

#define list_first_entry_or_null(ptr, type, field)  (list_empty(ptr) ? list_first_entry(ptr, type, field) : NULL)

typedef void *(*worker_routine_fn_t)(void*);
typedef struct thread_node thread_node_t;
typedef struct thread_worker thread_worker_t;
typedef struct thread_pool thread_pool_t;


struct thread_node {
    void *thread_arg;
    pthread_t thread;
    struct list_head list;
};

struct thread_worker {
    worker_routine_fn_t routine_fn;
    void *arg;
    void *retval;
    struct list_head list;
};

struct thread_pool {
    uint32_t thread_max_num;
    uint32_t thread_count;
    uint32_t thread_active_count;
    struct thread_node threads;
    struct thread_worker worker_ready;
    struct thread_worker worker_finished;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};


thread_pool_t *thread_pool_new(uint32_t thread_max_num){
    thread_pool_t *pool = malloc(sizeof(*pool));
    if(pool == NULL){
        return NULL;
    }
    memset(pool, 0, sizeof(*pool));

    INIT_LIST_HEAD(&(pool->threads.list));
    INIT_LIST_HEAD(&(pool->worker_ready.list));
    INIT_LIST_HEAD(&(pool->worker_finished.list));

    pool->thread_max_num = thread_max_num;

    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);
    return pool;
}

void thread_pool_destroy(thread_pool_t *pool){
    pthread_mutex_lock(&pool->lock);
    struct thread_worker *cur, *tmp;
    list_for_each_entry_safe(cur, tmp, &pool->worker_ready.list, list){
        list_del_init(&cur->list);
        free(cur);
    }
    list_for_each_entry_safe(cur, tmp, &pool->worker_finished.list, list){
        list_del_init(&cur->list);
        free(cur);
    }
    struct thread_node *cur_thread_node, *tmp_thread_node;
    list_for_each_entry_safe(cur_thread_node, tmp_thread_node, &pool->threads.list, list){
        pthread_t thread = cur_thread_node->thread;
        if(pthread_kill(thread, 0) == 0){
            pthread_cancel(thread);
            pthread_join(thread, NULL);
        }
        list_del_init(&cur_thread_node->list);
        free(cur_thread_node);
    }
    pthread_mutex_unlock(&pool->lock);
    pthread_cond_destroy(&pool->cond);
    pthread_mutex_destroy(&pool->lock);
    free(pool);
}

void *thread_routine(void *arg){
    thread_pool_t *pool = arg;

    for(;;){
        pthread_mutex_lock(&pool->lock);
        while(list_empty(&pool->worker_ready.list)){
            pthread_cond_wait(&pool->cond, &pool->lock);
        }

        thread_worker_t *worker = list_first_entry(&pool->worker_ready.list, thread_worker_t, list);
        list_del_init(&worker->list);
        pool->thread_active_count++;
        pthread_mutex_unlock(&pool->lock);

        pthread_cleanup_push(free, worker); //TODO: 释放 worker->arg

        if(worker->routine_fn != NULL){
            worker->retval = worker->routine_fn(worker->arg);
        }else{
            worker->retval = NULL;

            pthread_t pthread_id = pthread_self();
            DEBUG_LOG("tid: %u worker->routine is nullptr, arg is %p\n", (uint32_t)pthread_id, worker->arg);
        }

        pthread_cleanup_pop(0);

        pthread_mutex_lock(&pool->lock);
        pool->thread_active_count --;
        list_add_tail(&worker->list, &pool->worker_finished.list);
        pthread_mutex_unlock(&pool->lock);
    }
    return NULL;
}

int thread_pool_auto_new_thread(thread_pool_t *pool){
    pthread_t thread_id;
    int ret;

    sched_yield(); // 等待thread获取任务
    pthread_mutex_lock(&pool->lock);
    if(pool->thread_count >= pool->thread_max_num || list_empty(&pool->worker_ready.list)){
        pthread_mutex_unlock(&pool->lock);
        return EINVAL;
    }
    pthread_mutex_unlock(&pool->lock);


    if((ret = pthread_create(&thread_id, NULL, &thread_routine, pool)) == 0){
        thread_node_t *thread = malloc(sizeof(*thread));
        if(thread == NULL){
            return ENOMEM;
        }
        memset(thread, 0, sizeof(*thread));
        INIT_LIST_HEAD(&thread->list);
        thread->thread_arg = pool;

        pthread_mutex_lock(&pool->lock);
        list_add(&thread->list, &pool->threads.list);
        pool->thread_count++;
        pthread_mutex_unlock(&pool->lock);

    }
    return ret;
}

int thread_pool_add_worker(thread_pool_t *pool, worker_routine_fn_t routine, void *arg){
    struct thread_worker *worker = malloc(sizeof(*worker));
    if(worker == NULL){
        return ENOMEM;
    }
    memset(worker, 0, sizeof(*worker));
    INIT_LIST_HEAD(&worker->list);
    worker->routine_fn = routine;
    worker->arg = arg;

    pthread_mutex_lock(&pool->lock);
    list_add_tail(&worker->list, &pool->worker_ready.list);
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->lock);

    thread_pool_auto_new_thread(pool);

    return 0;
}

void thread_pool_wait(thread_pool_t *pool){
    bool finished_flag = false;
    while(! finished_flag){
        pthread_mutex_lock(&pool->lock);
        finished_flag = list_empty(&pool->worker_ready.list) && pool->thread_active_count == 0;
        pthread_mutex_unlock(&pool->lock);
    }
}

int thread_pool_release_finished_worker(thread_pool_t *pool, worker_routine_fn_t *fn, void **retval, void **arg){

    pthread_mutex_lock(&pool->lock);

    if( ! list_empty(&pool->worker_finished.list)){
        struct thread_worker *first = list_first_entry(&pool->worker_finished.list, struct thread_worker, list);
        list_del_init(&first->list);
        pthread_mutex_unlock(&pool->lock);

        if(fn != NULL){
            *fn = first->routine_fn;
        }
        if(retval != NULL){
            *retval = first->retval;
        }
        if(arg != NULL){
            *arg = first->arg;
        }
        free(first);
        return 0;
    }else{
        pthread_mutex_unlock(&pool->lock);
        return ENOENT;
    }
}


void *echo_worker(void *userdata){
    char *str = userdata;
    pthread_t tid = pthread_self();
    printf("%s: tid: %u echo: %s\n", __func__, (unsigned int)tid, str);
    sleep(1);

    return str;
}


int main()
{
    thread_pool_t *pool = thread_pool_new(2);

    thread_pool_add_worker(pool, echo_worker, strdup("bash"));
    thread_pool_add_worker(pool, echo_worker, strdup("mount"));
    thread_pool_add_worker(pool, echo_worker, strdup("cat"));
    thread_pool_add_worker(pool, echo_worker, strdup("pwd"));
    thread_pool_add_worker(pool, echo_worker, strdup("find"));
    thread_pool_add_worker(pool, echo_worker, strdup("vscode"));
    thread_pool_add_worker(pool, echo_worker, strdup("firefox"));

    thread_pool_wait(pool);

    void *arg;
    while(thread_pool_release_finished_worker(pool, NULL, NULL, &arg) == 0){
        printf("free( %s )\n", (char *)arg);
        free(arg);
    }

    thread_pool_destroy(pool);

    return 0;
}
