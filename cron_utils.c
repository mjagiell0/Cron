#include "cron_utils.h"

void *timer_thread(void *arg) {
    task_t *task = (task_t *) arg;
    char *argv[] = {task->exec_file_path, NULL};

    if (posix_spawn(&task->pid, task->exec_file_path, NULL, NULL, argv, NULL) != 0) {
        perror("posix_spawn failed");
    }

    task->active = 0;
    timer_delete(task->timer_id);
    return NULL;
}

void list_init(list_t *list) {
    list->head = NULL;
    list->count = 0;
}

void list_push(list_t *list, task_t task) {
    if (!list) return;

    node_t *node = calloc(1, sizeof(node_t));
    if (!node) {
        perror("calloc failed\n");
        return;
    }

    int time = time_value(&task.time_spec);

    struct sigevent timer_event;
    timer_event.sigev_notify = SIGEV_THREAD;
    timer_event.sigev_notify_function = (void (*)(__sigval_t)) timer_thread;
    timer_event.sigev_value.sival_ptr = &node->task;
    timer_event.sigev_notify_attributes = NULL;

    if (timer_create(CLOCK_REALTIME, &timer_event, &task.timer_id) == -1) {
        printf("Failed to create timer.\n");
        free(node);
        return;
    }

    struct itimerspec value;
    struct timespec rt_value;

    if (task.timer_type < 2)
        clock_gettime(CLOCK_REALTIME, &rt_value);
    else {
        rt_value.tv_nsec = 0;
        rt_value.tv_sec = 0;
    }

    value.it_value.tv_sec = rt_value.tv_sec + time;
    value.it_value.tv_nsec = 0;

    if (task.timer_type == I_RELATIVE || task.timer_type == I_ABSOLUTE) {
        value.it_interval.tv_sec = time;
        value.it_interval.tv_nsec = 0;
    }

    int flags;
    if (task.timer_type == I_RELATIVE || task.timer_type == RELATIVE)
        flags = 0;
    else
        flags = TIMER_ABSTIME;

    if (timer_settime(task.timer_id, flags, &value, NULL) == -1) {
        perror("timer_settime failed");
        timer_delete(node->task.timer_id);
        free(node);
        return;
    }

    node->task = task;
    node->task.active = 1;

    node->next = list->head;
    list->head = node;
    list->count++;
}

void list_clear(list_t *list) {
    if (!list)
        return;

    node_t *node = list->head;

    while (node) {
        node_t *next = node->next;
        timer_delete(node->task.timer_id);
        free(node);
        node = next;
    }

    list->head = NULL;
    list->count = 0;
}

void task_edit(list_t *list, task_t task, int idx) {
    if (!list || list_is_empty(list))
        return;

    node_t *node = list->head;
    for (int i = 0; i < idx; ++i)
        node = node->next;

    timer_delete(node->task.timer_id);
    int time = time_value(&task.time_spec);

    struct sigevent timer_event;
    timer_event.sigev_notify = SIGEV_THREAD;
    timer_event.sigev_notify_function = (void (*)(__sigval_t)) timer_thread;
    timer_event.sigev_value.sival_ptr = &node->task;
    timer_event.sigev_notify_attributes = NULL;

    if (timer_create(CLOCK_REALTIME, &timer_event, &task.timer_id) == -1) {
        printf("Failed to create timer.\n");
        free(node);
        return;
    }

    struct itimerspec value;
    struct timespec rt_value;

    if (task.timer_type < 2)
        clock_gettime(CLOCK_REALTIME, &rt_value);
    else {
        rt_value.tv_nsec = 0;
        rt_value.tv_sec = 0;
    }

    value.it_value.tv_sec = rt_value.tv_sec + time;
    value.it_value.tv_nsec = 0;

    if (task.timer_type == I_RELATIVE || task.timer_type == I_ABSOLUTE) {
        value.it_interval.tv_sec = time;
        value.it_interval.tv_nsec = 0;
    }

    int flags;
    if (task.timer_type == I_RELATIVE || task.timer_type == RELATIVE)
        flags = 0;
    else
        flags = TIMER_ABSTIME;

    if (timer_settime(task.timer_id, flags, &value, NULL) == -1) {
        perror("timer_settime failed");
        timer_delete(node->task.timer_id);
        free(node);
        return;
    }

    node->task = task;
    node->task.active = 1;
}

void tasks_display(task_t *tasks, unsigned int n) {
    if (n < 1) {
        printf("No tasks.\n");
    } else {
        printf("No. | min h d m wd | file name | timer type\n");
        printf("───────────────────────────────────────────\n");
        for (int i = 0; i < n; ++i) {
            task_t task = tasks[i];
            printf("%d. | ", i + 1);
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
                }
                case I_RELATIVE: {
                    printf("interval relative\n");
                }
            }
        }
    }
}

void list_print_to_file(list_t *list, FILE *f) {
    if (list_size(list) < 1) {
        fprintf(f, "No tasks.\n");
    } else {
        fprintf(f, "No. | min h d m wd | file name | timer type\n");
        fprintf(f, "───────────────────────────────────────────\n");
        node_t *node = list->head;
        int idx = 1;
        while (node) {
            task_t task = node->task;
            fprintf(f, "%d. | ", idx);
            if (task.time_spec.minute.is_asterisk) {
                fprintf(f, "* ");
            } else {
                fprintf(f, "%d ", task.time_spec.minute.val);
            }

            if (task.time_spec.hour.is_asterisk) {
                fprintf(f, "* ");
            } else {
                fprintf(f, "%d ", task.time_spec.hour.val);
            }

            if (task.time_spec.day.is_asterisk) {
                fprintf(f, "* ");
            } else {
                fprintf(f, "%d ", task.time_spec.day.val);
            }

            if (task.time_spec.month.is_asterisk) {
                fprintf(f, "* ");
            } else {
                fprintf(f, "%d ", task.time_spec.month.val);
            }

            if (task.time_spec.weekday.is_asterisk) {
                fprintf(f, "* |");
            } else {
                fprintf(f, "%d |", task.time_spec.weekday.val);
            }

            fprintf(f, " %s | ", task.exec_file_path);

            switch (task.timer_type) {
                case RELATIVE: {
                    fprintf(f, "relative\n");
                    break;
                }
                case ABSOLUTE: {
                    fprintf(f, "absolute\n");
                    break;
                }
                case I_ABSOLUTE: {
                    fprintf(f, "interval absolute\n");
                }
                case I_RELATIVE: {
                    fprintf(f, "interval relative\n");
                }
            }
            node = node->next;
        }
    }
}

int list_is_empty(list_t *list) {
    return !list || !list->head || list->count == 0;
}

int list_size(list_t *list) {
    if (!list || list_is_empty(list))
        return -1;

    return list->count;
}

void list_remove_index(list_t *list, int idx) {
    if (!list || list_is_empty(list) || list_size(list) < idx)
        return;

    node_t *prev = NULL;
    node_t *node = list->head;
    for (int i = 0; i < idx; ++i) {
        prev = node;
        node = node->next;
    }

    if (prev) {
        prev->next = node->next;
    } else if (node->next) {
        list->head = node->next;
    } else {
        list->head = NULL;
    }

    timer_delete(node->task.timer_id);
    free(node);
    list->count--;
}

void list_destroy(list_t *list) {
    if (!list)
        return;

    node_t *node = list->head;

    while (node) {
        node_t *next = node->next;
        timer_delete(node->task.timer_id);
        free(node);
        node = next;
    }
}

int time_spec_validate(ctime_spec_t *time_spec, char time_data[5][10]) {
    return minute_validate(&time_spec->minute, time_data[0]) && hour_validate(&time_spec->hour, time_data[1]) &&
           day_validate(&time_spec->day, time_data[2]) && month_validate(&time_spec->month, time_data[3]) &&
           weekday_validate(&time_spec->weekday, time_data[4]);
}

int minute_validate(ctime_spec_val_t *time_spec_minute, char *minute) {
    int8_t val = atoi(minute);
    int is_digit = isdigit(*minute);
    if (*minute == '*') {
        time_spec_minute->val = val;
        time_spec_minute->is_asterisk = 1;
    } else if (val >= 0 && val <= 59 && is_digit) {
        time_spec_minute->val = val;
        time_spec_minute->is_asterisk = 0;
    } else {
        return 0;
    }
    return 1;
}

int hour_validate(ctime_spec_val_t *time_spec_hour, char *hour) {
    int8_t val = atoi(hour);
    int is_digit = isdigit(*hour);
    if (*hour == '*') {
        time_spec_hour->val = val;
        time_spec_hour->is_asterisk = 1;
    } else if (val >= 0 && val <= 23 && is_digit) {
        time_spec_hour->val = val;
        time_spec_hour->is_asterisk = 0;
    } else {
        return 0;
    }
    return 1;
}

int day_validate(ctime_spec_val_t *time_spec_day, char *day) {
    int8_t val = atoi(day);
    int is_digit = isdigit(*day);
    if (*day == '*') {
        time_spec_day->val = val;
        time_spec_day->is_asterisk = 1;
    } else if (val >= 1 && val <= 31 && is_digit) {
        time_spec_day->val = val;
        time_spec_day->is_asterisk = 0;
    } else {
        return 0;
    }
    return 1;
}

int month_validate(ctime_spec_val_t *time_spec_month, char *month) {
    int8_t val = atoi(month);
    int is_digit = isdigit(*month);
    if (*month == '*') {
        time_spec_month->val = val;
        time_spec_month->is_asterisk = 1;
    } else if (val >= 1 && val <= 12 && is_digit) {
        time_spec_month->val = val;
        time_spec_month->is_asterisk = 0;
    } else {
        return 0;
    }
    return 1;
}

int weekday_validate(ctime_spec_val_t *time_spec_weekday, char *weekday) {
    int8_t val = atoi(weekday);
    int is_digit = isdigit(*weekday);
    if (*weekday == '*') {
        time_spec_weekday->val = val;
        time_spec_weekday->is_asterisk = 1;
    } else if (val >= 1 && val <= 7 && is_digit) {
        time_spec_weekday->val = val;
        time_spec_weekday->is_asterisk = 0;
    } else {
        return 0;
    }
    return 1;
}

void trim(char *str) {
    int i = 0;
    while (isspace((unsigned char) str[i])) {
        i++;
    }
    if (i > 0) {
        memmove(str, str + i, strlen(str) - i + 1);
    }
}

int time_value(ctime_spec_t *time_spec) {
    int time = 0;

    if (!time_spec->minute.is_asterisk)
        time += time_spec->minute.val * 60;

    if (!time_spec->hour.is_asterisk)
        time += time_spec->hour.val * 60 * 60;

    if (!time_spec->day.is_asterisk)
        time += time_spec->day.val * 60 * 60 * 24;

    if (!time_spec->month.is_asterisk)
        time += time_spec->month.val * 60 * 60 * 24 * 31;

    if (!time_spec->weekday.is_asterisk)
        time += time_spec->weekday.val * 60 * 60 * 24;

    if (!time)
        time = 60;

    return time;
}
