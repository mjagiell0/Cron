#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include <semaphore.h>

#define TRUE  (1)
#define FALSE (0)

#define DATETIME_BUFFER_SIZE (17)
#define DUMP_FILENAME_SIZE (DATETIME_BUFFER_SIZE + 19)
#define LOG_FILENAME_SIZE (DATETIME_BUFFER_SIZE + 19)

#define FILE_APPEND_MODE "a"
#define DUMP_PREFIX "dump_"
#define LOG_PREFIX "log_"
#define LOG_EXTENSION ".log"


/*
 * Wartości określające flagi działania systemu
 * */
typedef enum {
    OFF,
    ON
} log_state_t;

/*
 * Struktura przechowywyująca informacje o systemie logowania
 */
typedef struct {
    FILE *logfile;
    log_priority_t priority;
    log_state_t state;
} logger_t;

/*
 * Funkcja wykonywana przez wątek odpowiedzialny
 * za pliki dump.
 * */
void *log_dump_thread_func(void *arg);

/*
 * Funkcja wykonywana przez wątek odpowiedzialny
 * za stan systemu logowania plików.
 * */
void *log_switch_thread_func(void *arg);

/*
 * Funkcja wykonywana przez wątek odpowiedzialny
 * za zmiane priorytetu zapisywanych logów.
 * */
void *log_priority_thread_func(void *arg);

/*
 * Handler sygnału wykonującego plik dump
 * */
void log_dump_signal_handler(int signum);

/*
 * Handler sygnału do zmiany stanu systemu logowania
 * plików.
 * */
void log_switch_signal_handler(int signum);

/*
 * Handler sygnału do zmiany priorytetu zapisywanych
 * logów.
 * */
void log_priority_signal_handler(int signum);

/*
 * Handler sygnału utylizujący system logowania plików
 * */
void log_terminate_signal_handler(int signum);

/*
 * Funkcja zapisująca aktualą data do zmiennej
 * date_buffer.
 * */
void datetime(void);

static logger_t logger;

static atomic_int flag_logger_init = FALSE;
static atomic_int flag_logger_dump = FALSE;
static atomic_int flag_logger_switch = FALSE;
static atomic_int flag_logger_priority = FALSE;
static atomic_int flag_logger_terminate = FALSE;
static atomic_int flag_logger_change_dump_func = FALSE;


static pthread_t log_dump_thread;
static pthread_t log_switch_thread;
static pthread_t log_priority_thread;

static pthread_mutex_t log_dump_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_switch_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_logfile_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_priority_mutex = PTHREAD_MUTEX_INITIALIZER;

static sem_t log_dump_sem;
static sem_t log_switch_sem;
static sem_t log_priority_sem;

static log_dump_func_t state_dump_callback = NULL;

static char datetime_buffer[DATETIME_BUFFER_SIZE];

void log_init(const char *filename, log_dump_func_t callback, void *dump_args) {
    if (atomic_load(&flag_logger_init) == TRUE) {
        errno = EFAULT;
        return;
    }

    if (!filename) {
        char log_filename[LOG_FILENAME_SIZE] = {0};

        datetime();
        sprintf(log_filename, "%s%d_%s%s",LOG_PREFIX, getpid(), datetime_buffer, LOG_EXTENSION);

        filename = log_filename;
    }

    logger.logfile = fopen(filename, FILE_APPEND_MODE);
    if (!logger.logfile) {
        errno = EIO;
        return;
    }

    logger.priority = MAX;
    logger.state = ON;

    if (callback)
        state_dump_callback = callback;

    sem_t *sems[3] = {&log_dump_sem, &log_switch_sem, &log_priority_sem};
    for (int i = 0; i < 3; ++i) {
        if (sem_init(sems[i],0,0) == -1) {
            for (int j = 0; j < i; ++j) {
                sem_destroy(sems[j]);
            }
            fclose(logger.logfile);
            errno = EINVAL;
            return;
        }
    }

    pthread_create(&log_priority_thread, NULL, log_priority_thread_func, NULL);
    pthread_create(&log_switch_thread, NULL, log_switch_thread_func, NULL);
    pthread_create(&log_dump_thread, NULL, log_dump_thread_func, dump_args);

    struct sigaction sa;
    sigset_t set;

    sigfillset(&set);

    sa.sa_handler = log_priority_signal_handler;
    sa.sa_mask = set;
    sa.sa_flags = 0;

    sigaction(LOG_PRIORITY_SIGNAL, &sa, NULL);

    sa.sa_handler = log_switch_signal_handler;
    sigaction(LOG_SWITCH_SIGNAL, &sa, NULL);

    sa.sa_handler = log_dump_signal_handler;
    sigaction(LOG_DUMP_SIGNAL, &sa, NULL);

    sa.sa_handler = log_terminate_signal_handler;
    sigaction(LOG_TERMINATE_SIGNAL, &sa, NULL);

    atomic_store(&flag_logger_init, TRUE);
}

void log_dump_signal_handler(int signum) {
    atomic_store(&flag_logger_dump, TRUE);
    sem_post(&log_dump_sem);
}

void log_switch_signal_handler(int signum) {
    atomic_store(&flag_logger_switch, TRUE);
    sem_post(&log_switch_sem);
}

void log_priority_signal_handler(int signum) {
    atomic_store(&flag_logger_priority, TRUE);
    sem_post(&log_priority_sem);
}

void log_terminate_signal_handler(int signum) {
    atomic_store(&flag_logger_terminate, TRUE);
    sem_post(&log_dump_sem);
    sem_post(&log_switch_sem);
    sem_post(&log_priority_sem);
}

void *log_dump_thread_func(void *arg) {
    sigset_t set;
    sigfillset(&set);
    sigdelset(&set, LOG_DUMP_SIGNAL);
    pthread_sigmask(SIG_SETMASK, &set, NULL);

    while (TRUE) {
        sem_wait(&log_dump_sem);
        if (atomic_load(&flag_logger_dump) == TRUE) {
            if (state_dump_callback) {
                pthread_mutex_lock(&log_dump_mutex);
                char dump_filename[DUMP_FILENAME_SIZE] = {0};

                datetime();
                sprintf(dump_filename, "%s%d_%s%s",DUMP_PREFIX, getpid(), datetime_buffer, LOG_EXTENSION);

                state_dump_callback(dump_filename, arg);
                pthread_mutex_unlock(&log_dump_mutex);
            }
            atomic_store(&flag_logger_dump, FALSE);
        }

        if (atomic_load(&flag_logger_change_dump_func) == TRUE || atomic_load(&flag_logger_terminate) == TRUE) {
            atomic_store(&flag_logger_change_dump_func, FALSE);
            break;
        }
        sleep(1);
    }
    return NULL;
}


void *log_switch_thread_func(void *arg) {
    sigset_t set;

    sigfillset(&set);
    sigdelset(&set, LOG_SWITCH_SIGNAL);
    pthread_sigmask(SIG_SETMASK, &set, NULL);

    while (TRUE) {
        sem_wait(&log_switch_sem);
        if (atomic_load(&flag_logger_switch) == TRUE) {
            pthread_mutex_lock(&log_switch_mutex);
            logger.state = logger.state == ON ? OFF : ON;
            pthread_mutex_unlock(&log_switch_mutex);
            atomic_store(&flag_logger_switch, FALSE);
        }

        if (atomic_load(&flag_logger_terminate) == TRUE) {
            break;
        }
        sleep(1);
    }

    return NULL;
}

void *log_priority_thread_func(void *arg) {
    sigset_t set;

    sigfillset(&set);
    sigdelset(&set, LOG_PRIORITY_SIGNAL);
    pthread_sigmask(SIG_SETMASK, &set, NULL);

    while (TRUE) {
        sem_wait(&log_priority_sem);
        if (atomic_load(&flag_logger_priority) == TRUE) {
            pthread_mutex_lock(&log_priority_mutex);
            logger.priority = (logger.priority + 1) % 3;
            pthread_mutex_unlock(&log_priority_mutex);
            atomic_store(&flag_logger_priority, FALSE);
        }

        if (atomic_load(&flag_logger_terminate) == TRUE) {
            break;
        }
        sleep(1);
    }

    return NULL;
}


int lprintf(log_priority_t priority, const char *format, ...) {
    pthread_mutex_lock(&log_priority_mutex);
    if (priority > logger.priority) {
        pthread_mutex_unlock(&log_priority_mutex);
        return 0;
    }
    pthread_mutex_unlock(&log_priority_mutex);

    pthread_mutex_lock(&log_state_mutex);
    if (logger.state == OFF) {
        pthread_mutex_unlock(&log_state_mutex);
        return 0;
    }
    pthread_mutex_unlock(&log_state_mutex);

    char *priority_sign;

    switch (priority) {
        case LOW:
            priority_sign = "#";
            break;
        case MID:
            priority_sign = "##";
            break;
        case MAX:
            priority_sign = "###";
            break;
    }

    va_list args;
    va_start(args, format);

    datetime();

    pthread_mutex_lock(&log_logfile_mutex);
    int result = fprintf(logger.logfile, "[%3s]<%s>", priority_sign, datetime_buffer);
    vfprintf(logger.logfile, format, args);
    pthread_mutex_unlock(&log_logfile_mutex);

    va_end(args);

    return result;
}

void log_register_state_dump_callback(log_dump_func_t callback, void *args) {
    pthread_mutex_lock(&log_dump_mutex);

    if (callback) {
        state_dump_callback = callback;
    }

    if (args) {
        atomic_store(&flag_logger_change_dump_func, TRUE);
        sem_post(&log_dump_sem);
        pthread_join(log_dump_thread, NULL);

        pthread_create(&log_dump_thread, NULL, log_dump_thread_func, args);
    }
    pthread_mutex_unlock(&log_dump_mutex);
}

void datetime(void) {
    strcpy(datetime_buffer,"");

    time_t curr_time = time(NULL);
    struct tm *time_info = localtime(&curr_time);

    strftime(datetime_buffer, 20, "%y-%m-%d_%H.%M.%S", time_info);
}

void log_set_default_settings(void) {
    pthread_mutex_lock(&log_priority_mutex);
    pthread_mutex_lock(&log_state_mutex);
    logger.priority = MAX;
    logger.state = ON;
    pthread_mutex_unlock(&log_priority_mutex);
    pthread_mutex_unlock(&log_state_mutex);
}

void log_close(void) {
    if (atomic_load(&flag_logger_init) == FALSE)
        return;

    raise(LOG_TERMINATE_SIGNAL);
    pthread_join(log_dump_thread, NULL);
    pthread_join(log_switch_thread, NULL);
    pthread_join(log_priority_thread, NULL);
    struct sigaction sa;

    sa.sa_handler = SIG_DFL;
    sa.sa_flags = 0;

    sigaction(LOG_PRIORITY_SIGNAL, &sa, NULL);
    sigaction(LOG_SWITCH_SIGNAL, &sa, NULL);
    sigaction(LOG_DUMP_SIGNAL, &sa, NULL);

    atomic_store(&flag_logger_init, FALSE);
    fclose(logger.logfile);
}

