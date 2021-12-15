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

typedef void *(*task_routine_fn_t)(void*);
typedef struct thread_node thread_node_t;
typedef struct thread_task thread_task_t;
typedef struct thread_pool thread_pool_t;


struct thread_node {
    void *thread_arg;
    pthread_t thread;
    struct list_head list;
};

struct thread_task {
    task_routine_fn_t routine_fn;
    void *arg;
    void *retval;
    struct list_head list;
};

struct thread_pool {
    uint32_t thread_max_num;
    uint32_t thread_count;
    uint32_t thread_active_count;
    struct thread_node threads;
    struct thread_task task_ready;
    struct thread_task task_finished;
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
    INIT_LIST_HEAD(&(pool->task_ready.list));
    INIT_LIST_HEAD(&(pool->task_finished.list));

    pool->thread_max_num = thread_max_num;

    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);
    return pool;
}

void thread_pool_destroy(thread_pool_t *pool){
    pthread_mutex_lock(&pool->lock);
    struct thread_task *cur, *tmp;
    list_for_each_entry_safe(cur, tmp, &pool->task_ready.list, list){
        list_del_init(&cur->list);
        free(cur);
    }
    list_for_each_entry_safe(cur, tmp, &pool->task_finished.list, list){
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
        while(list_empty(&pool->task_ready.list)){
            pthread_cond_wait(&pool->cond, &pool->lock);
        }

        thread_task_t *task = list_first_entry(&pool->task_ready.list, thread_task_t, list);
        list_del_init(&task->list);
        pool->thread_active_count++;
        pthread_mutex_unlock(&pool->lock);

        pthread_cleanup_push(free, task); //TODO: 释放 task->arg

        if(task->routine_fn != NULL){
            task->retval = task->routine_fn(task->arg);
        }else{
            task->retval = NULL;

            pthread_t pthread_id = pthread_self();
            DEBUG_LOG("tid: %u task->routine is nullptr, arg is %p\n", (uint32_t)pthread_id, task->arg);
        }

        pthread_cleanup_pop(0);

        pthread_mutex_lock(&pool->lock);
        pool->thread_active_count --;
        list_add_tail(&task->list, &pool->task_finished.list);
        pthread_mutex_unlock(&pool->lock);
    }
    return NULL;
}

int thread_pool_auto_new_thread(thread_pool_t *pool){
    pthread_t thread_id;
    int ret;

    sched_yield(); // 等待thread获取任务
    pthread_mutex_lock(&pool->lock);
    if(pool->thread_count >= pool->thread_max_num || list_empty(&pool->task_ready.list)){
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
        thread->thread = thread_id;

        pthread_mutex_lock(&pool->lock);
        list_add(&thread->list, &pool->threads.list);
        pool->thread_count++;
        pthread_mutex_unlock(&pool->lock);

    }
    return ret;
}

int thread_pool_add_task(thread_pool_t *pool, task_routine_fn_t routine, void *arg){
    struct thread_task *task = malloc(sizeof(*task));
    if(task == NULL){
        return ENOMEM;
    }
    memset(task, 0, sizeof(*task));
    INIT_LIST_HEAD(&task->list);
    task->routine_fn = routine;
    task->arg = arg;

    pthread_mutex_lock(&pool->lock);
    list_add_tail(&task->list, &pool->task_ready.list);
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->lock);

    thread_pool_auto_new_thread(pool);

    return 0;
}

void thread_pool_wait(thread_pool_t *pool){
    bool finished_flag = false;
    while(! finished_flag){
        pthread_mutex_lock(&pool->lock);
        finished_flag = list_empty(&pool->task_ready.list) && pool->thread_active_count == 0;
        pthread_mutex_unlock(&pool->lock);
    }
}

int thread_pool_release_finished_task(thread_pool_t *pool, task_routine_fn_t *fn, void **retval, void **arg){

    pthread_mutex_lock(&pool->lock);

    if( ! list_empty(&pool->task_finished.list)){
        struct thread_task *first = list_first_entry(&pool->task_finished.list, struct thread_task, list);
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


void *echo_task(void *userdata){
    char *str = userdata;
    pthread_t tid = pthread_self();
    printf("%s: tid: %u echo: %s\n", __func__, (unsigned int)tid, str);
    sleep(1);

    return str;
}


int main()
{
    thread_pool_t *pool = thread_pool_new(2);

    thread_pool_add_task(pool, echo_task, strdup("bash"));
    thread_pool_add_task(pool, echo_task, strdup("mount"));
    thread_pool_add_task(pool, echo_task, strdup("cat"));
    thread_pool_add_task(pool, echo_task, strdup("pwd"));
    thread_pool_add_task(pool, echo_task, strdup("find"));
    thread_pool_add_task(pool, echo_task, strdup("vscode"));
    thread_pool_add_task(pool, echo_task, strdup("firefox"));

    thread_pool_wait(pool);

    void *arg;
    while(thread_pool_release_finished_task(pool, NULL, NULL, &arg) == 0){
        printf("free( %s )\n", (char *)arg);
        free(arg);
    }

    thread_pool_destroy(pool);

    return 0;
}
