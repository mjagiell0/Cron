#include "cron_utils.h"

static list_t list;

// Mutexes
static pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

// Semaphores
static sem_t *server_free = NULL;
static sem_t process_sem;

// Message buffer
static msgbuf_t server_msgbuf;

// Example dump function
void dump_func(const char *filename, void *args) {
    FILE *f = fopen(filename,"w");
    if (!f) {
        printf("Failed to create dump file\n");
        return;
    }

    pthread_mutex_lock(&list_mutex);
    list_print_to_file(&list, f);
    pthread_mutex_unlock(&list_mutex);
    fclose(f);
}

int main(int argc, char **argv) {
    server_free = sem_open(SEM_NAME, O_RDWR);
    if (server_free == SEM_FAILED && fork() != 0) {
        server_free = sem_open(SEM_NAME, O_CREAT | O_EXCL | O_RDONLY, 0666, 2);
        if (server_free == SEM_FAILED) {
            printf("Failed to create semaphore.\n");
            return 1;
        }

        mq_attr_t mq_attr = {.mq_curmsgs = 0, .mq_msgsize = sizeof(msgbuf_t), .mq_maxmsg = MSG_MAX_COUNT, .mq_flags = 0};
        mqd_t mqd = mq_open((const char *) QUEUE_NAME, O_CREAT | O_EXCL | O_RDWR, 0666, &mq_attr);
        if (mqd == -1) {
            printf("Failed to create queue.\n");
            return 1;
        }

        if (sem_init(&process_sem, 0, 0) == -1) {
            printf("Failed to init thread semaphore.\n");
            mq_close(mqd);
            mq_unlink(QUEUE_NAME);
            return 1;
        }

        list_init(&list);

        log_init(NULL,dump_func,&list);

        printf("PID: %d\n", getpid());

        int end = 0;

        while (!end) {
            mq_receive(mqd, (char *) &server_msgbuf, sizeof(msgbuf_t), 0);
            switch (server_msgbuf.mtype) {
                case ADD: {
                    lprintf(MID,"[PID:%d]: Add\n", server_msgbuf.pid);
                    pthread_mutex_lock(&list_mutex);
                    list_push(&list, server_msgbuf.task);
                    pthread_mutex_unlock(&list_mutex);
                    break;
                }
                case DELETE: {
                    lprintf(MID,"[PID:%d]: Delete\n", server_msgbuf.pid);
                    pthread_mutex_lock(&list_mutex);
                    if (server_msgbuf.idx == -1)
                        list_clear(&list);
                    else
                        list_remove_index(&list, server_msgbuf.idx);
                    pthread_mutex_unlock(&list_mutex);
                    break;
                }
                case EDIT: {
                    lprintf(MID,"[PID:%d]: Edit\n", server_msgbuf.pid);
                    pthread_mutex_lock(&list_mutex);
                    task_edit(&list, server_msgbuf.task, server_msgbuf.idx);
                    pthread_mutex_unlock(&list_mutex);
                    break;
                }
                case LIST: {
                    lprintf(MID,"[PID:%d]: List\n", server_msgbuf.pid);
                    char client_mq_name[CLIENT_MQ_NAME_LEN];
                    sprintf(client_mq_name, "%s%d", CLIENT_QUEUE_PREFIX, server_msgbuf.pid);

                    mqd_t client_mqd = mq_open(client_mq_name, O_WRONLY, 0666);
                    if (client_mqd == -1) {
                        lprintf(LOW,"[PID:%d]: Failed to connect with client queue.\n", server_msgbuf.pid);
                        break;
                    }

                    response_t response;
                    pthread_mutex_lock(&list_mutex);
                    node_t *node = list.head;

                    if (!node) {
                        response.is_next = -1;
                        mq_send(client_mqd, (char *) &response, sizeof(response_t), 0);
                    }

                    while (node) {
                        response.task = node->task;
                        node = node->next;
                        response.is_next = node ? 1 : 0;
                        mq_send(client_mqd, (char *) &response, sizeof(response_t), 0);
                    }

                    pthread_mutex_unlock(&list_mutex);
                    mq_close(client_mqd);

                    break;
                }
                case CLOSE_CLIENT: {
                    lprintf(MID,"[PID:%d]: Close client\n", server_msgbuf.pid);
                    sem_post(server_free);

                    break;
                }
                case DESTROY: {
                    lprintf(MID,"[PID:%d]: Close\n", server_msgbuf.pid);
                    end = 1;
                    break;
                }
            }
        }

        sem_destroy(&process_sem);

        sem_close(server_free);
        sem_unlink(SEM_NAME);

        mq_close(mqd);
        mq_unlink(QUEUE_NAME);

        log_close();

        list_destroy(&list);
    } else {
        sem_wait(server_free);

        msgbuf_t msgbuf = {.pid = getpid()};

        mqd_t server_mqd = mq_open(QUEUE_NAME, O_WRONLY, 0666);
        if (server_mqd == -1) {
            printf("Failed to open queue.\n");
            sem_close(server_free);
            return 1;
        }

        if (argc > 1) {
            char client_mq_name[CLIENT_MQ_NAME_LEN];
            sprintf(client_mq_name, "%s%d", CLIENT_QUEUE_PREFIX, msgbuf.pid);

            mq_attr_t mq_attr = {.mq_maxmsg = 10, .mq_msgsize = sizeof(response_t), .mq_flags = 0, .mq_curmsgs = 0};

            mqd_t client_mqd = mq_open(client_mq_name, O_CREAT | O_EXCL | O_RDONLY, 0666, &mq_attr);
            if (client_mqd == -1) {
                printf("Failed to create client queue.\n");
                msgbuf.mtype = CLOSE_CLIENT;

                mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);

                sem_close(server_free);
                mq_close(server_mqd);
                return 1;
            }

            char *flag = argv[1];

            if (strcmp(flag, ADD_FLAG) == 0 && argc == 3) { // Adding task
                msgbuf.mtype = ADD;
                char *timer_type_flag = argv[2];
                task_t task;
                if (strcmp(timer_type_flag, ABSOLUTE_TIMER_FLAG) == 0)
                    task.timer_type = ABSOLUTE;
                else if (strcmp(timer_type_flag, RELATIVE_TIMER_FLAG) == 0)
                    task.timer_type = RELATIVE;
                else if (strcmp(timer_type_flag, I_RELATIVE_TIMER_FLAG) == 0)
                    task.timer_type = I_RELATIVE;
                else if (strcmp(timer_type_flag, I_ABSOLUTE_TIMER_FLAG) == 0)
                    task.timer_type = I_ABSOLUTE;
                else {
                    printf("Incorrect timer type flag.\n");

                    msgbuf.mtype = CLOSE_CLIENT;
                    mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
                    mq_close(client_mqd);
                    mq_unlink(client_mq_name);
                    mq_close(server_mqd);
                    sem_close(server_free);

                    return 1;
                }

                char time_data[5][10];
                char *text = "┌──────────── minutes (0 - 59)\n"
                             "│ ┌──────────── hours (0 - 23)\n"
                             "│ │ ┌──────────── months day (1 - 31)\n"
                             "│ │ │ ┌──────────── month (1 - 12)\n"
                             "│ │ │ │ ┌──────────── weekday  (1 - 7) (monday - sunday)\n"
                             "│ │ │ │ │\n"
                             "│ │ │ │ │\n"
                             "│ │ │ │ │\n";
                printf("%s", text);

                for (int i = 0; i < 5; ++i) {
                    scanf("%9s", time_data[i]);
                }

                scanf("%254[^\n]", task.exec_file_path);

                trim(task.exec_file_path);

                if (time_spec_validate(&task.time_spec, time_data) == 0) {
                    printf("Incorrect time specification.\n");

                    msgbuf.mtype = CLOSE_CLIENT;
                    mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
                } else if (access(task.exec_file_path, F_OK) == -1) {
                    printf("Incorrect file path.\n");

                    msgbuf.mtype = CLOSE_CLIENT;
                    mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
                } else {
                    msgbuf.task = task;
                    mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);

                    msgbuf.mtype = CLOSE_CLIENT;
                    mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
                }
            } else if (strcmp(flag, LIST_FLAG) == 0) { // Show list of tasks
                response_t response;
                msgbuf.mtype = LIST;

                mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
                mq_receive(client_mqd, (char *) &response, sizeof(response_t), 0);

                if (response.is_next == -1)
                    printf("No tasks.\n");
                else {
                    int counter = 0;

                    printf("No. | min h d m wd | file name | timer type\n");
                    printf("───────────────────────────────────────────\n");
                    while (1) {
                        printf("%d. | ", counter + 1);
                        task_t task = response.task;
                        if (task.time_spec.minute.is_asterisk) {
                            printf("* ");
                        } else {
                            printf("%d ", task.time_spec.minute.val);
                        }

                        if (task.time_spec.hour.is_asterisk) {
                            printf("* ");
                        } else {
                            printf("%d ", task.time_spec.hour.val);
                        }

                        if (task.time_spec.day.is_asterisk) {
                            printf("* ");
                        } else {
                            printf("%d ", task.time_spec.day.val);
                        }

                        if (task.time_spec.month.is_asterisk) {
                            printf("* ");
                        } else {
                            printf("%d ", task.time_spec.month.val);
                        }

                        if (task.time_spec.weekday.is_asterisk) {
                            printf("* |");
                        } else {
                            printf("%d |", task.time_spec.weekday.val);
                        }

                        printf(" %s | ", task.exec_file_path);

                        switch (task.timer_type) {
                            case RELATIVE: {
                                printf("relative\n");
                                break;
                            }
                            case ABSOLUTE: {
                                printf("absolute\n");
                                break;
                            }
                            case I_ABSOLUTE: {
                                printf("interval absolute\n");
                                break;
                            }
                            case I_RELATIVE: {
                                printf("interval relative\n");
                                break;
                            }
                        }

                        if (!response.is_next)
                            break;
                        mq_receive(client_mqd, (char *) &response, sizeof(response_t), 0);
                        counter++;
                    }
                }

                msgbuf.mtype = CLOSE_CLIENT;
                mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
            } else if (strcmp(flag, DELETE_FLAG) == 0) {
                if (argc == 3) { // Deleting one task
                    int idx = atoi(argv[2]) - 1;

                    if (idx == -1) {
                        printf("Incorrect index value.\n");
                    } else {
                        msgbuf.mtype = DELETE;
                        msgbuf.idx = idx;

                        int result = mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
                        if (result == 0) {
                            printf("Successful deleting %d. task.\n", idx + 1);
                        } else {
                            printf("Failed to delete %d. task.\n", idx + 1);
                        }
                    }
                } else { // Deleting all tasks
                    printf("Do you want to delete all tasks (y/n)?:\n");
                    char c;
                    if (scanf("%c", &c) != 1) {
                        printf("Incorrect input.\n");
                    } else if (c == 'y') {
                        msgbuf.mtype = DELETE;
                        msgbuf.idx = -1;

                        int result = mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
                        if (result == 1) {
                            printf("Successful deleting all tasks.\n");
                        } else {
                            printf("Failed to delete tasks.\n");
                        }
                    }
                }

                msgbuf.mtype = CLOSE_CLIENT;
                mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
            } else if (strcmp(flag, EDIT_FLAG) == 0 && argc == 4) { // Edit task
                msgbuf.mtype = EDIT;
                int idx = atoi(argv[2]) - 1;

                if (idx == -1) {
                    printf("Incorrect index value.\n");
                } else {
                    msgbuf.idx = idx;
                    char *timer_type_flag = argv[3];
                    task_t task;
                    if (strcmp(timer_type_flag, ABSOLUTE_TIMER_FLAG) == 0)
                        task.timer_type = ABSOLUTE;
                    else if (strcmp(timer_type_flag, RELATIVE_TIMER_FLAG) == 0)
                        task.timer_type = RELATIVE;
                    else if (strcmp(timer_type_flag, I_RELATIVE_TIMER_FLAG) == 0)
                        task.timer_type = I_RELATIVE;
                    else if (strcmp(timer_type_flag, I_ABSOLUTE_TIMER_FLAG) == 0)
                        task.timer_type = I_ABSOLUTE;
                    else {
                        printf("Incorrect timer type flag.\n");

                        msgbuf.mtype = CLOSE_CLIENT;
                        mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
                        mq_close(client_mqd);
                        mq_unlink(client_mq_name);
                        mq_close(server_mqd);
                        sem_close(server_free);

                        return 1;
                    }

                    char time_data[5][10];
                    char *text = "┌──────────── minutes (0 - 59)\n"
                                 "│ ┌──────────── hours (0 - 23)\n"
                                 "│ │ ┌──────────── months day (1 - 31)\n"
                                 "│ │ │ ┌──────────── month (1 - 12)\n"
                                 "│ │ │ │ ┌──────────── weekday  (1 - 7) (monday - sunday)\n"
                                 "│ │ │ │ │\n"
                                 "│ │ │ │ │\n"
                                 "│ │ │ │ │\n";
                    printf("%s", text);

                    for (int i = 0; i < 5; ++i)
                        scanf("%9s", time_data[i]);

                    scanf("%254[^\n]", task.exec_file_path);

                    trim(task.exec_file_path);

                    if (time_spec_validate(&task.time_spec, time_data) == 0) {
                        printf("Incorrect time specification.\n");

                        msgbuf.mtype = CLOSE_CLIENT;
                        mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
                    } else if (access(task.exec_file_path, F_OK) == -1) {
                        printf("Incorrect file path.\n");

                        msgbuf.mtype = CLOSE_CLIENT;
                        mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
                    } else {
                        msgbuf.task = task;
                        mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);

                        msgbuf.mtype = CLOSE_CLIENT;
                        mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
                    }
                }

                msgbuf.mtype = CLOSE_CLIENT;
                mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
            } else if (strcmp(flag, DESTROY_FLAG) == 0) { // Close cron
                msgbuf.mtype = DESTROY;
                mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
            } else {
                printf("Incorrect flag.\n");
                msgbuf.mtype = CLOSE_CLIENT;

                mq_send(server_mqd, (char *) &msgbuf, sizeof(msgbuf_t), 0);
            }
            mq_close(client_mqd);
            mq_unlink(client_mq_name);
        } else {
            printf("Server is already working.\n");
            printf("Client options:\n");
            printf("-a -[tr/ta/tir/tia] - add task with relative/absolute/relative interval/absolute interval timer type\n");
            printf("-e [task index] -[tr/ta/tir/tia] - edit task at index to relative/absolute/relative interval/absolute interval timer type\n");
            printf("-r ([task index]) - remove all tasks or task at index (if specified)\n");
            printf("-l - display tasks list\n");
            printf("-d - close cron server\n");
        }

        mq_close(server_mqd);
        sem_close(server_free);
    }
}
