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
    uint32_t thread_max_num;  //允许的最大线程数量
    uint32_t thread_count;  //当前活动的线程数量 （没有被 cancel 或者 pthread_exit 的进程数量）
    uint32_t thread_active_count; // 当前正在运行用户任务的线程数量
    uint32_t thread_cancel_count;  // cancel 或者 exit的线程数量
    struct thread_node threads;
    struct thread_task task_ready;
    struct thread_task task_finish;
    struct thread_task task_cancel;

    pthread_mutex_t lock;
    pthread_cond_t cond;

    bool running_flag;
};


thread_pool_t *thread_pool_new(uint32_t thread_max_num){
    thread_pool_t *pool = malloc(sizeof(*pool));
    if(pool == NULL){
        return NULL;
    }
    memset(pool, 0, sizeof(*pool));

    INIT_LIST_HEAD(&(pool->threads.list));
    INIT_LIST_HEAD(&(pool->task_ready.list));
    INIT_LIST_HEAD(&(pool->task_finish.list));
    INIT_LIST_HEAD(&(pool->task_cancel.list));

    pool->thread_max_num = thread_max_num;
    pool->running_flag = 1;

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
    list_for_each_entry_safe(cur, tmp, &pool->task_finish.list, list){
        list_del_init(&cur->list);
        free(cur);
    }
    list_for_each_entry_safe(cur, tmp, &pool->task_cancel.list, list){
        list_del_init(&cur->list);
        free(cur);
    }

    pool->running_flag = false;

    struct thread_node *cur_thread_node, *tmp_thread_node;
    list_for_each_entry_safe(cur_thread_node, tmp_thread_node, &pool->threads.list, list){
        pthread_t thread = cur_thread_node->thread;

        pthread_mutex_unlock(&pool->lock);

        pthread_cancel(thread);
        if(pthread_kill(thread, 0) == 0){
            pthread_cond_broadcast(&pool->cond);
        }
        pthread_join(thread, NULL);

        pthread_mutex_lock(&pool->lock);

        list_del_init(&cur_thread_node->list);
        free(cur_thread_node);
    }
    pthread_mutex_unlock(&pool->lock);
    pthread_cond_destroy(&pool->cond);
    pthread_mutex_destroy(&pool->lock);
    free(pool);
}

struct pthread_cleanup_data{
    thread_pool_t *pool;
    thread_task_t *task;
};


thread_task_t *_thread_routine_get_readytask_cond(thread_pool_t *pool){
    pthread_mutex_lock(&pool->lock);

    while(pool->running_flag && list_empty(&pool->task_ready.list)){
        pthread_cond_wait(&pool->cond, &pool->lock);
    }
    if(! pool->running_flag){
        pthread_mutex_unlock(&pool->lock);
        DEBUG_LOG("normal pthread_exit: %u\n", (unsigned int) pthread_self());
        pthread_exit(NULL);
    }

    thread_task_t *task = list_first_entry(&pool->task_ready.list, thread_task_t, list);
    list_del_init(&task->list);
    pool->thread_active_count++;

    pthread_mutex_unlock(&pool->lock);
    return task;
}

void _thread_routine_put_finishtask(thread_pool_t *pool, thread_task_t *task){
    pthread_mutex_lock(&pool->lock);
    pool->thread_active_count --;
    list_add_tail(&task->list, &pool->task_finish.list);
    pthread_mutex_unlock(&pool->lock);
}

void _thread_routine_put_canceltask(void *cleanup_data){
    struct pthread_cleanup_data *data = cleanup_data;
    thread_pool_t *pool = data->pool;
    thread_task_t *task = data->task;

    pthread_mutex_lock(&pool->lock);
    pool->thread_active_count --;
    pool->thread_count --;
    pool->thread_cancel_count ++;
    list_add_tail(&task->list, &pool->task_cancel.list);
    pthread_mutex_unlock(&pool->lock);

    DEBUG_LOG("pthread_cancel: %u\n", (unsigned int) pthread_self());
}


void *thread_routine(void *arg){
    thread_pool_t *pool = arg;

    int old_cancel_state;

    while(pool->running_flag){
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancel_state); //cond_wait时禁止cancel， 防止锁资源释放错误
        thread_task_t *task = _thread_routine_get_readytask_cond(pool);
        pthread_setcancelstate(old_cancel_state, NULL); // 支持在运行用户task时，支持cancel

        struct pthread_cleanup_data cleanup_data = {
            .pool = pool,
            .task = task
        };
        int old_cancel_type;
        pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, &old_cancel_type); // see pthread_cleanup_push_defer_np
        pthread_cleanup_push(_thread_routine_put_canceltask, &cleanup_data);

        if(task->routine_fn != NULL){
            task->retval = task->routine_fn(task->arg);
        }else{
            task->retval = NULL;
            DEBUG_LOG("tid: %u task->routine is nullptr, arg is %p\n", (uint32_t)pthread_self(), task->arg);
        }

        pthread_cleanup_pop(0);
        pthread_setcanceltype(old_cancel_type, NULL);

        _thread_routine_put_finishtask(pool, task);
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

int thread_pool_release_canceledthread(thread_pool_t *pool){
    pthread_mutex_lock(&pool->lock);

    // TODO:

    pthread_mutex_unlock(&pool->lock);
    return 0;
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

    if( ! list_empty(&pool->task_finish.list)){
        struct thread_task *first = list_first_entry(&pool->task_finish.list, struct thread_task, list);
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

int thread_pool_release_canceled_task(thread_pool_t *pool, task_routine_fn_t *fn, void **retval, void **arg){
    pthread_mutex_lock(&pool->lock);

    if( ! list_empty(&pool->task_cancel.list)){
        struct thread_task *first = list_first_entry(&pool->task_cancel.list, struct thread_task, list);
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
    sleep(5);

    tid = tid / 100;
    if(tid % 2 == 1){
        pthread_exit(NULL);
    }
    return str;
}


int main()
{
    thread_pool_t *pool = thread_pool_new(5);

    thread_pool_add_task(pool, echo_task, strdup("bash"));
    thread_pool_add_task(pool, echo_task, strdup("mount"));
    thread_pool_add_task(pool, echo_task, strdup("cat"));
    thread_pool_add_task(pool, echo_task, strdup("pwd"));
    thread_pool_add_task(pool, echo_task, strdup("find"));

    thread_pool_add_task(pool, echo_task, strdup("vscode"));
    thread_pool_add_task(pool, echo_task, strdup("firefox"));
    thread_pool_add_task(pool, echo_task, strdup("qt-creator"));
    thread_pool_add_task(pool, echo_task, strdup("anjuta"));
    thread_pool_add_task(pool, echo_task, strdup("gedit"));

    thread_pool_add_task(pool, echo_task, strdup("libreoffice"));

    thread_pool_wait(pool);

    void *arg;
    while(thread_pool_release_finished_task(pool, NULL, NULL, &arg) == 0){
        printf("free finished_task ( %s )\n", (char *)arg);
        free(arg);
    }

    while(thread_pool_release_canceled_task(pool, NULL, NULL, &arg) == 0){
        printf("free canceled_task ( %s )\n", (char *)arg);
        free(arg);
    }

    thread_pool_destroy(pool);

    return 0;
}
