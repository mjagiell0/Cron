# Cron: notatki

## Treść projektu
Celem projektu jest napisanie programu do harmonogramowania zadań będącego funkcjonalnym odpowiednikiem programu cron (https://pl.wikipedia.org/wiki/Cron) znanego z systemów unixowych. Program powinien spełniać następujące założenia:
- powinien być oparty na mechanizmie zegarów interwałowych;
  
- sterowanie pracą programu powinno odbywać przez argumenty wiersza poleceń;
  
- powinno być możliwe uruchomienie tylko jednej instancji programu (pierwsze uruchomienie pliku wykonywalnego uruchamia serwer usługi, kolejne są klientami);
  
- kolejne uruchomienia pliku wykonywalnego z odpowiednimi argumentami służyć mają sterowaniu pracą programu;
  
- komunikacja klient-serwer ma przebiegać z wykorzystaniem mechanizmu komunikatów;
  
- powinno być możliwe planowanie uruchomienia wskazanego programu poprzez podanie czasu względnego, bezwzględnego lub cyklicznie;
  
- musi istnieć możliwość planowanego uruchomienia programu z argumentami wiersza poleceń;
  
- program musi mieć możliwość wyświetlenia listy zaplanowanych zadań;
  
- musi istnieć możliwość anulowania wybranego zadania;
  
- program powinien mieć możliwość zapisywania logów z wykorzystaniem biblioteki zrealizowane w ramach projektu 1;
  
- zakończenie pracy programu powinno być równoznaczne z anulowaniem wszystkich zaplanowanych zadań;
  
- aplikacja powinna być zgodna ze standardami POSIX i języka C.

## Mechanizm zegrów interwałowych
*#include <time.h>*<br>
**timer_t** - struktura obiektu zegara interwałowego.<br>
**struct sigevent** - struktura określająca zadanie wykonywane po skończeniu odliczania przez zegar.<br>

| Pole sigevent         | Sygnały             | Wątki                                         |
|-----------------------|---------------------|-----------------------------------------------|
| sigev_notify          | SIGEV_SIGNAL        | SIGEV_THREAD                                  |
| sigev.signo           | (*wartość sygnału*) | -                                             |
| sigev_notify_function | -                   | (*wskaźnik do funkcji*) / *NULL*              |
| sigev_value.sival_ptr | -                   | (*wskaźnik na argumenty do funckji*) / *NULL* |
| sigev_no              | -                   | (*atrybuty wątku*) / *NULL*                   |


**timer_create** (CLOCK_REALTIME, sigevent, timer) - funkcja tworząca obiekt zegara.

**struct itimespec** - struktura opisująca działanie zegara w czasie (za ile, co ile).

| Pole itimespec     | Opis                                                                            |
|--------------------|---------------------------------------------------------------------------------|
| it_value.tv_sec    | Określa **po jakim** czasie zegar ma<br> wywoływać przypisaną do siebie funkcję |
| it_interval.tv_sec | Określa **co jaki** czas zegar ma<br> wywoływać przypisaną do siebie funkcję    |

**timer_settime (timer, 0, itimerspec, NULL)** - ustawia czas zegara poprzez itimespec oraz uruchamia go.

## Wieloprocesowość
*#include <unistd.h>*

**fork ()** - funckja tworząca proces potomny od procesu w którym została wywołana. Proces potomny ma id równe 0.<br>
**wait ()** - funckja wstrzymująca działanie procesu macierzystego od momentu zakończenia działania procesu potomnego.<br>
**pipe (int[2])** - funkcja tworzy kanał komunikacyjny między procesami zapisywany w tablicy dwóch int'ów. Pierwszy element odnosi się do kanału czytania, drugi do kanału pisania.<br>
**write (fd[1], ptr, sizeof)** - funkcja zapisuje do kanału zapisu *sizeof* byte'ów danych spod adresu *ptr*.<br>
**read (fd[0], ptr, sizeof)** - funckja czytająca z kanału odczytu *sizeof* byte'ów do wskaźnika *ptr*.<br>
**close (fd)** - funckja zamyka kanał określony wartością kanału fd.<br>

Jeżeli proces nie czyta/zapisuje nic z/do kanału danych to na początku powinien zamykać kanał odczytu/zapisu.<br>

## Komunikacja międzyprocesowa
*#include <mqueue.h>*

**mqd_t** - id obiektu kolejki utowrzonej lub otwartej za pomocą *mq_open* (coś jak z fd dla pamięci współdzielonej)<br>
**struct mq_attr** - stukruta opisująca obiekt kolejki wiadomości. 

| Pole mq_attr    | Opis                                                                                                                                  |
|-----------------|---------------------------------------------------------------------------------------------------------------------------------------|
| long mq_flags   | Zawiera flagi powiązane z opisem otwartej kolejki wiadomości. Jest inicjalizowana jeśli kolejka została utowrzona poprzez **mq_open** |
| long mq_maxmsg  | Limit wiadomości w kolejce. Ustawiane przy funkcji **mq_open**.                                                                       |
| long mq_msgsize | Limit rozmiaru jednej wiadomości w kolejce.                                                                                           |
| long mq_curmsgs | Licznik wiadomości aktualnie znajdujących się w kolejce.                                                                              |

**mq_open (\*name, oflags, mode, \*attr)** - funkcja tworzy lub otwiera kolejkę wiadomości.<br>
**mq_receive (mqd_t, \*msg_ptr, msg_size, msg_prio)** - usuwa z kolejki *mqd_t* najstarszą wiadomość z najwyższym priorytetem *msg_prio* i przekazuje ją pod wskaźnik *msg_ptr*.<br>
**mq_send (mqd_t, \*msq_ptr, msg_size, msg_prio)** - wstawia do kolejki *mqd_t* wiadomość pod wskaźnikiem *msg_ptr* o rozmiarze *msg_size* i priorytecie *msg_prio*.<br>
**mq_close (mqd_t)** - zamyka połączenie z kolejką *mqd_t*.<br>
**mq_unlink(\*name)** - niszczy obiekt kolejki pod nazwą *name*.<br>

