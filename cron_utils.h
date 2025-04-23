#ifndef CRON_CRON_UTILS_H
#define CRON_CRON_UTILS_H

#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <mqueue.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ctype.h>
#include <spawn.h>
#include "logger.h"

// Defines
#define PROCESS_SIG (SIGRTMIN)
#define TERMINATE_SIG (SIGRTMIN + 1)
#define TRUE (1)
#define FALSE (0)
#define MSG_MAX_COUNT (10)
#define MAX_TASKS_COUNT (10)
#define CLIENT_MQ_NAME_LEN (20)
#define EXEC_FILE_PATH_LEN (255)
#define ADD_FLAG "-a"
#define LIST_FLAG "-l"
#define EDIT_FLAG "-e"
#define DELETE_FLAG "-r"
#define DESTROY_FLAG "-d"
#define ABSOLUTE_TIMER_FLAG "-ta"
#define RELATIVE_TIMER_FLAG "-tr"
#define I_ABSOLUTE_TIMER_FLAG "-tia"
#define I_RELATIVE_TIMER_FLAG "-tir"

// Names
#define SEM_NAME "/sem_name"
#define QUEUE_NAME "/queue_name"
#define CLIENT_QUEUE_PREFIX "/queue_"

// Typedefs
typedef struct mq_attr mq_attr_t;
typedef struct node_t node_t;

// Enums
typedef enum {
    ADD,
    DELETE,
    EDIT,
    LIST,
    CLOSE_CLIENT,
    DESTROY
} mtype_t;

typedef enum {
    ABSOLUTE,
    I_ABSOLUTE,
    RELATIVE,
    I_RELATIVE
} timer_type_t;

// Structures
typedef struct {
    int8_t val;
    int8_t is_asterisk;
} ctime_spec_val_t;

typedef struct {
    ctime_spec_val_t minute;
    ctime_spec_val_t hour;
    ctime_spec_val_t day;
    ctime_spec_val_t month;
    ctime_spec_val_t weekday;
} ctime_spec_t;

typedef struct {
    ctime_spec_t time_spec;
    timer_type_t timer_type;
    timer_t timer_id;
    pid_t pid;
    int8_t active;
    char exec_file_path[EXEC_FILE_PATH_LEN];
} task_t;

typedef struct {
    mtype_t mtype;
    task_t task;
    pid_t pid;
    int idx;
} msgbuf_t;

typedef struct {
    task_t task;
    int is_next;
} response_t;

struct node_t {
    task_t task;
    node_t *next;
};

typedef struct {
    node_t *head;
    int count;
} list_t;


// List methods
void list_init(list_t *list);

void list_push(list_t *list, task_t task);

void list_pop(list_t *list, task_t *result);

void list_clear(list_t *list);

void list_display(list_t *list);

void list_print_to_file(list_t *list, FILE *f);

void tasks_display(task_t *tasks, unsigned int n);

int list_size(list_t *list);

void task_edit(list_t *list, task_t task,int idx);

int list_is_empty(list_t *list);

void list_remove_index(list_t *list, int idx);

void list_destroy(list_t *list);

int time_spec_validate(ctime_spec_t *time_spec, char time_data[5][10]);

int minute_validate(ctime_spec_val_t *time_spec_minute, char *minute);

int hour_validate(ctime_spec_val_t *time_spec_hour, char *hour);

int day_validate(ctime_spec_val_t *time_spec_day, char *day);

int month_validate(ctime_spec_val_t *time_spec_month, char *month);

int weekday_validate(ctime_spec_val_t *time_spec_weekday, char *weekday);

void trim(char *str);

int time_value(ctime_spec_t *time_spec);

#endif //CRON_CRON_UTILS_H
