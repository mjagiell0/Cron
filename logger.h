#ifndef PROJEKT_SYSTEM_LOGOWANIA_PLIKOW_LOGGER_H
#define PROJEKT_SYSTEM_LOGOWANIA_PLIKOW_LOGGER_H

#define LOG_SWITCH_SIGNAL       (SIGRTMIN)
#define LOG_DUMP_SIGNAL         (SIGRTMIN + 1)
#define LOG_PRIORITY_SIGNAL     (SIGRTMIN + 2)
#define LOG_TERMINATE_SIGNAL    (SIGRTMIN + 3)

/*
 * Wartości określające priorytet komunikatu logowania
 * */
typedef enum {
    LOW,
    MID,
    MAX
} log_priority_t;

/*
 * Zdefiniowany typ uchwytu do funckji dump.
 * */
typedef void (*log_dump_func_t)(const char *filename, void *args);

/*
 * Funkcja inicjalizująca systom logowania do
 * plików.
 * */
void log_init(const char *filename, log_dump_func_t callback, void *dump_args);

/*
 * Funkcja zapisująca treść logu do pliku
 * logowania.
 * */
int lprintf(log_priority_t priority, const char *format, ...);

/*
 * Funkcja określająca zachowanie funckji dump z
 * opcjonalnymmi argumentami.
 * */
void log_register_state_dump_callback(log_dump_func_t callback, void *args);

/*
 * Funkcja zamykająca system logowania plików.
 * */
void log_close(void);

/*
 * Funkcja przywracająca domyślny priorytet
 * zapisywanych logów.
 * */
void log_set_default_settings(void);

#endif
