/* Łukasz Kania 148077
 * projekt PSW - problem producenta - konsumenta na wielu procesach
 * kompilacja:  gcc -o projekt projekt.c -lpthread -lrt -Wall
 * uruchomienie: ./projekt
 * lub: ./projekt 10
 * lub: ./projekt 10 2
 * lub: ./projekt 10 2 4
 * pierwszy argument: rozmiar bufora (opcjonalny, domyślny: 5)
 * drugi argument: liczba producentów (opcjonalny, domyślny: 1)
 * trzeci argument: liczba konsumentów (opcjonalny, domyślny: 1)
 * zachęcam do ustawiania różnych parametrów, zwłaszcza flagi VERBOSE na 1
 * oraz liczby/czasu maksymalnych produktów do produkcji/konsumpcji
 * warunkowe skończenie programu wykrywające wyczerpanych producentów lub konsumentów
 * wykrywa pusty/pełny potok przy zakończeniu się programu
 * Program obsługuje sygnał SIGINT (uwaga - obsługa usuwa pliki z semaforami i z pamięcią)
 *
 * Pamięć jest podzielona na trzy bloki:
 * - blok indeksów wolnych
 * - blok indeksów zajętych
 * - blok produktów
 * Przy wypisywaniu (VERBOSE 1):
 * - F oznacza Front - pierwszy element kolejki
 * - R oznacza Rear - ostatni element kolejki
 * - S oznacza Size - rozmiar kolejki
 *
 * Jeśliby coś nie działało:
 * Program napisano na systemie Manjaro Linux 21.2.3
 * Kernel: Linux 5.16.7-1-MANJARO
*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include "queue.h"

#define DEFAULT_BUFFER_SIZE 5 //domyślny rozmiar bufora - "magazynu"
#define DEFAULT_PRODUCERS_NUMBER 1 //domyślna liczba producentów
#define DEFAULT_CONSUMERS_NUMBER 1 //domyślna liczba konsumentów
#define MIN_ITEM_NUMBER 1 //minimalny nr wyprodukowanego przedmiotu
#define MAX_ITEM_NUMBER 99 //maksymalny nr wyprodukowanego przedmiotu
#define MIN_PRODUCING_TIME_MS 500 //minimalny czas produkcji
#define MAX_PRODUCING_TIME_MS 1500 //maksymalny czas produkcji
#define MAX_PRODUCTS_PRODUCED 10 //maksymalna liczba produktów możliwa do wyprodukowania przez jednego producenta
#define MIN_CONSUMING_TIME_MS 500 //minimalny czas konsumpcji
#define MAX_CONSUMING_TIME_MS 1500 //maksymalny czas konsumpcji
#define MAX_PRODUCTS_CONSUMED 5 //maksymalna liczba produktów możliwa do konsumpcji przez jednego konsumenta
#define VERBOSE 0 //flaga, która ustawia powiadomienia o zakończonych producentach i konsumentach oraz o ich liczbie
int variables_memory = 0; //globalna zmienna - deskryptor pamięci współdzielonej dla zmiennych (zmienne te przechowują ilość wyczerpanych producentów i konsumentów)
int buffer = 0; //globalne zmienna - deskryptor pamięci współdzielonej dla bufora
int mutex_fd[4] = {0}; //deskryptory dla mutexów
int *ptr_var = NULL; //wskaźnik na zmapowaną pamięć współdzieloną zmiennych
int *ptr_buffer = NULL; //wskaźnik na zmapowaną pamięć współdzieloną dla bufora
void print_shm(int fd, int size, int flag);

void producer(sem_t *full, sem_t *empty, int buff_size, pthread_mutex_t *buffer_mutex, pthread_mutex_t *producer_mutex, int id, Queue *empty_queue, Queue *full_queue) {
    for (int i= 0; i < MAX_PRODUCTS_PRODUCED; i++) {
        int a = 0, buffer_full = 0, first_free = 0, item = 0;

        //najpierw producent ubiega się o przyznanie mu wolnego indeksu - osobna sekcja krytyczna wyłącznie dla producentów
        //producenci muszą odczytać różne wolne indeksy
        pthread_mutex_lock(producer_mutex);
        //printf("PRODUCENT %d WEJŚCIE DO SEKCJI PRODUCENTÓW\n", id);

        if (!isEmpty(empty_queue)) {
            first_free = front(empty_queue); //wyszukanie pierwszego wolnego indeksu
            //while (first_free == -1)
            //    first_free = front(empty_queue);
            //printf("PROD FIRST FREE: front: %3d", first_free);

            pthread_mutex_lock(empty_queue->lseek_mutex);
            //zagnieżdżona sekcja krytyczna - jej konieczność wynika z tego, że
            //w międzyczasie inny proces mógłby przesunąć lseekiem wskaźnik dla bufora
            lseek(empty_queue->fd, first_free, SEEK_SET);
            //jeżeli tutaj nastąpiłaby zmiana kontekstu, to odczyt mógłby nastąpić w złym miejscu
            read(empty_queue->fd, &first_free, 1);

            pthread_mutex_unlock(empty_queue->lseek_mutex);

            //printf(" idx: %3d\n", first_free);
            //zdjęcie indeksu z kolejki wolnych indeksów - operacja jest atomowa w definicji dequeue(1)
            a = dequeue(empty_queue);
        } else {
            //jeżeli bufor jest pełen to proces kończy ubieganie się o wolny indeks
            //printf("PRODUCENT: BUFOR PEŁEN!\n");
            buffer_full = 1;
            i--;
        }
        //printf("PRODUCENT %d WYJŚCIE Z SEKCJI PRODUCENTÓW\n", id);
        pthread_mutex_unlock(producer_mutex);

        if (!buffer_full) { //bufor nie może być pełny, żeby produkować
            item = rand() % (MAX_ITEM_NUMBER - MIN_ITEM_NUMBER + 1) + MIN_ITEM_NUMBER; //losowy przedmiot
            usleep(1000 * (rand() % (MAX_PRODUCING_TIME_MS - MIN_PRODUCING_TIME_MS + 1) + MIN_PRODUCING_TIME_MS)); //czasochłonna produkcja

            sem_wait(empty); //opuszczenie semafora pustego - sygnalizacja dla konsumentów że coś jest w magazynie

            /* wejście do sekcji krytycznej (relikt z poprzedniej wersji, program będzie też działał z odkomentowaną tą linią, ale
             * nie ma potrzeby zamykania dostępu dla konsumentów, bo oni na pewno nie znajdą się w tym momencie w konflikcie
             * z producentami, bo każdy i tak ma przyznany swój indeks, który jest już usunięty z kolejki wolnych indeksów
             * identycznie zostawiłem w konsumencie
            */
            //pthread_mutex_lock(buffer_mutex);

            enqueue(full_queue, a); //aktualizacja kolejki indeksów zajętych - dodanie do niej przyznanego indeksu

            pthread_mutex_lock(empty_queue->lseek_mutex);
            //dodanie przedmiotu na otrzymanym indeksie
            lseek(empty_queue->fd, first_free, SEEK_SET);
            write(empty_queue->fd, &item, 1); //umieszczenie produktu na pierwszym wolnym indeksie
            pthread_mutex_unlock(empty_queue->lseek_mutex);

            if (VERBOSE) {
                pthread_mutex_lock(empty_queue->lseek_mutex);
                //wyświetlenie pamięci - operacja musi być atomowa
                print_shm(empty_queue->fd, buff_size, 0);
                pthread_mutex_unlock(empty_queue->lseek_mutex);
            }
            printf("Producer %2d: produces item %3d produced items: %3d/%d | PID: %5d\n", id, item, i+1, MAX_PRODUCTS_PRODUCED, getpid());

            //pthread_mutex_unlock(buffer_mutex); //wyjście z sekcji krytycznej (relikt z poprzedniej wersji)
            sem_post(full); //podniesienie semafora pełnego
        }
    }
    if (VERBOSE) { //producent wyczerpany
        printf("PRODUCER %2d DEPLETED\n", id);
    }
    if (kill(getpid(), SIGUSR1) == -1) { //wysyła sygnał
        exit(1);
    }
}

void consumer(sem_t *full, sem_t *empty, int buff_size, pthread_mutex_t *buffer_mutex, pthread_mutex_t *consumer_mutex, int id, Queue *empty_queue, Queue *full_queue) {
    for (int i = 0; i < MAX_PRODUCTS_CONSUMED; i++) {
        int item = 0, a = 0, first_full = 0, buffer_empty = 0;

        pthread_mutex_lock(consumer_mutex); //sekcja krytyczna dla konsumentów ubiegających się o przyznanie indeksu

        if (!isEmpty(full_queue)) {
            first_full = front(full_queue); //pobranie pierwszego zajętego indeksu

            pthread_mutex_lock(empty_queue->lseek_mutex);
            lseek(empty_queue->fd, first_full, SEEK_SET);
            //komplementarna sytuacja jak w producencie
            read(empty_queue->fd, &first_full, 1);
            pthread_mutex_unlock(empty_queue->lseek_mutex);

            //zdjęcie indeksu z kolejki zajętych indeksów
            a = dequeue(full_queue);
        } else {
            //ustawienie flagi - konsument kończy ubieganie się o przyznanie indeksu, bo magazyn i tak jest pusty
            buffer_empty = 1;
            i--;
        }

        pthread_mutex_unlock(consumer_mutex);

        if (!buffer_empty) {//bufor nie może być pusty żeby konsumować
            sem_wait(full); //opuszczenie semafora pełny - zmniejszenie jego wartości o 1
            //pthread_mutex_lock(buffer_mutex); //wejście do sekcji krytycznej - pozostałość po poprzedniej wersji
            pthread_mutex_lock(empty_queue->lseek_mutex);
            lseek(empty_queue->fd, first_full, SEEK_SET);
            //pobranie produktu
            read(empty_queue->fd, &item, 1);
            pthread_mutex_unlock(empty_queue->lseek_mutex);


            printf("%70s Consumer %2d: consumes item: %3d consumed items: %3d/%d | PID: %5d\n", " ", id, item, i+1, MAX_PRODUCTS_CONSUMED, getpid());
            usleep(1000 * (rand() % (MAX_CONSUMING_TIME_MS - MIN_CONSUMING_TIME_MS + 1) + MIN_CONSUMING_TIME_MS)); //czasochłonna konsumpcja

            enqueue(empty_queue, a); //aktualizacja kolejki indeksów wolnych - dodanie do niej przyznanego indeksu

            if (VERBOSE) {
                pthread_mutex_lock(empty_queue->lseek_mutex);
                print_shm(empty_queue->fd, buff_size, 1);
                pthread_mutex_unlock(empty_queue->lseek_mutex);
            }

            //pthread_mutex_unlock(buffer_mutex); //wyjście z sekcji krytycznej
            sem_post(empty);
        }
    }
    if (VERBOSE) { //konsument wyczerpany
        printf("%70s CONSUMER %2d DEPLETED\n", " ", id);
    }
    if (kill(getpid(), SIGUSR2) == -1) { //wysyła sygnał
        exit(1);
    }
}

sem_t *initSemaphore(const char *name, int value) {
    //inizjalizacja semafora o danej nazwie i wartości początkowej
    sem_t *sem = sem_open(name, O_CREAT, 0600, value);
    if (sem == SEM_FAILED) {
        perror("Error: sem_open");
        exit(-1);
    }
    return sem;
}

void destroySemaphore(sem_t *sem, const char *name) {
    //usuwa semafor o podanej nazwie
    sem_destroy(sem);
    munmap(sem, sizeof(sem));
    sem_close(sem);
    sem_unlink(name);
}

int initShm(char *name, int size) {
    int mem_fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (mem_fd == -1) { //błąd otwarcia pamięci
        perror("Error: failed to create shared memory");
        exit(-1);
    }
    if (ftruncate(mem_fd, size) == -1) { //ustawienie rozmiaru pamięci na dwa bajty
        perror("Error: shared memory ftruncate failed");
        exit(-1);
    }
    return mem_fd;
}

int *mapShm(int size, int fd) {
    int *mapped_shm = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_shm == MAP_FAILED) { //błąd mapowania
        perror("Error: shared memory map failed");
        exit(-1);
    }
    return mapped_shm;
}

void keyboardInterrupt() {
    //w przypadku sygnału SIGINT usuwa pliki z semaforami oraz z pamięcią współdzieloną
    system("rm /dev/shm/sem.empty_sem /dev/shm/sem.full_sem /dev/shm/var_shm /dev/shm/buffer /dev/shm/mutex_shm* 2> /dev/null");
    exit(0);
}

void detectProducersDepletion() {
    //pobiera pierwszy bajt z pamięci współdzielonej jako liczbę wyczerpanych producentów
    int data = 0;
    lseek(variables_memory, 0, SEEK_SET);
    if (read(variables_memory, &data, 1) < 0) {
        perror("Error: unable to read from shm");
    }
    data += 1;
    lseek(variables_memory, 0, SEEK_SET);
    write(variables_memory, &data, 1);
    if (VERBOSE) {
        printf("PRODUCERS DEPLETED SO FAR: %d\n", data);
    }
}

void detectConsumersDepletion() {
    //pobiera drugi bajt z pamięci współdzielonej jako liczbę wyczerpanych konsumentów
    int data = 0;
    lseek(variables_memory, 1, SEEK_SET);
    if (read(variables_memory, &data, 1) < 0) {
        perror("Error: unable to read from shm");
    }
    data += 1;
    lseek(variables_memory, 1, SEEK_SET);
    write(variables_memory, &data, 1);
    if (VERBOSE) {
        printf("%70s CONSUMERS DEPLETED SO FAR: %d\n", " ", data);
    }
}

void print_shm(int fd, int size, int flag) {
    //wypisanie stanu pamięci bufora
    lseek(fd, 0, SEEK_SET);
    int address = 0, global_idx = 0;

    if (flag) printf("%70s ", " ");
    printf("Free indexes: \n");
    if (flag) printf("%70s ", " ");
    for (int i = global_idx; i < size + global_idx + 3; i++)
        printf("%3d ", i);
    printf("\n");
    if (flag) printf("%70s ", " ");
    global_idx += size + 3;
    for (int i = 0; i < size; i++)
        printf("%3d ", i);
    printf("  F   R   S\n");
    if (flag) printf("%70s ", " ");
    for (int i = 0; i < size + 3; i++) {
        read(fd, &address, 1);
        printf("%3d ", address);
    }
    printf("\n");
    if (flag) printf("%70s ", " ");
    printf("Occupied indexes: \n");
    if (flag) printf("%70s ", " ");
    for (int i = global_idx; i < size + global_idx + 3; i++)
        printf("%3d ", i);
    printf("\n");
    if (flag) printf("%70s ", " ");
    global_idx += size + 3;
    for (int i = 0; i < size; i++)
        printf("%3d ", i);
    printf("  F   R   S\n");
    if (flag) printf("%70s ", " ");
    for (int i = 0; i < size + 3; i++) {
        read(fd, &address, 1);
        printf("%3d ", address);
    }
    printf("\n");
    if (flag) printf("%70s ", " ");
    printf("Products: \n");
    if (flag) printf("%70s ", " ");
    for (int i = global_idx; i < size + global_idx; i++)
        printf("%3d ", i);
    printf("\n");
    if (flag) printf("%70s ", " ");
    global_idx += size;
    for (int i = 0; i < size; i++)
        printf("%3d ", i);
    printf("\n");
    if (flag) printf("%70s ", " ");
    for (int i = 0; i < size; i++) {
        read(fd, &address, 1);
        printf("%3d ", address);
    }
    printf("\n\n");
}

int main(int argc, char *argv[])
{
    srand(time(NULL) ^ (getpid()<<16)); //unikatowe ziarno
    signal(SIGINT, keyboardInterrupt); //zmiana obsługi sygnałów
    signal(SIGUSR1, detectProducersDepletion);
    signal(SIGUSR2, detectConsumersDepletion);

    int buff_size = DEFAULT_BUFFER_SIZE; //rozmiar bufora
    int producers = DEFAULT_PRODUCERS_NUMBER; //liczba producentów
    int consumers = DEFAULT_CONSUMERS_NUMBER; //liczba konsumentów
    if (argc > 4) { //nieprawidłowa liczba argumentów
        perror("Error: invalid number of arguments");
        exit(-1);
    }

    switch (argc) { //obsługa argumentów
        case 1:
            break;
        case 4:
            consumers = (int)strtol(argv[3], (char**) NULL, 10);
        case 3:
            producers = (int)strtol(argv[2], (char**) NULL, 10);
        case 2:
            buff_size = (int)strtol(argv[1], (char**) NULL, 10);
            break;
        default:
            break;
    }

    if (consumers <= 0 || producers <= 0 || buff_size <= 0) { //nieprawidłowe argumenty
        perror("Error: Invalid arguments");
        exit(-1);
    }

    sem_t *empty = initSemaphore("/empty_sem", buff_size); //semafor "pusty"
    sem_t *full = initSemaphore("/full_sem", 0); //semafor "pełny"

    variables_memory = initShm("/var_shm", 2); //otwarcie pliku pamięci współdzielonej dla zmiennych
    ptr_var = mapShm(2, variables_memory); //zmapowanie bloku pamięci współdzielonej

    buffer = initShm("/buffer", buff_size * 3 + 6); //otwarcie pliku pamięci współdzielonej dla bufora
    ptr_buffer = mapShm(buff_size * 3 + 6, buffer); //zmapowanie bloku pamięci współdzielonej

    pthread_mutex_t *buffer_mutex[4] = {NULL}; //zamki do pamięci, producentów, konsumentów i kolejek
    for (int i = 0; i < 4; i++) {
        char mutex_name[15];
        snprintf(mutex_name, 15, "/mutex_shm_%d", i);
        /* /mutex_shm_0 - mutex do całego bufora (nieużywany - relikt z poprzedniej wersji, ale go zostawiłem)
         * /mutex_shm_1 - mutex dla producentów (żeby tylko jeden producent naraz otrzymywał wolny indeks)
         * /mutex_shm_2 - mutex dla konsumentów (żeby tylko jeden konsument naraz otrzymywał zajęty inteks)
         * /mutex_shm_3 - mutex dla poszczególnych operacji w kolejce (żeby na pewno procesor nie zmienił kontekstu
         *                między np. lseek a write, ponieważ wtedy inny proces może zmienić wskaźnik w pliku)
        */
        mutex_fd[i] = initShm(mutex_name, sizeof(pthread_mutex_t)); //inicjalizacja zamka
        buffer_mutex[i] = (pthread_mutex_t *) mapShm(sizeof(pthread_mutex_t), mutex_fd[i]); //zmapowanie mutexa
        //nadawanie atrybutu dla zamka - domyślnie mutex nie jest dzielony między procesami
        pthread_mutexattr_t attrmutex;
        pthread_mutexattr_init(&attrmutex);
        pthread_mutexattr_setpshared(&attrmutex, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(buffer_mutex[i], &attrmutex); //inicjalizacja zamka
        pthread_mutexattr_destroy(&attrmutex);
    }

    //kolejka indeksów wolnych
    Queue *empty_queue = initQueue(buff_size, buffer, 0, buff_size + 2, buffer_mutex[3]);
    //kolejka indeksów zajętych
    Queue *full_queue = initQueue(buff_size, buffer, buff_size + 3, 2 * buff_size + 5, buffer_mutex[3]);
    int fd[2]; //deskryptory dla potoku, który zawiera PID-y procesów potomnych
    pipe(fd); //ma to na celu uniknięcie zawieszonych procesów po zakończeniu głównego procesu
    int pipe_size = fcntl(fd[1], F_SETPIPE_SZ, producers + consumers); //ustawianie rozmiaru potoku na zadany
    if (pipe_size == -1) {
        perror("Error: setting pipe size");
        exit(-1);
    }

    for (int i = 0; i < buff_size; i++)
        enqueue(empty_queue, i + 2 * buff_size + 6); //dodanie wolnych indeksów do kolejki wolnych indeksów

    if (VERBOSE)
        print_shm(buffer, buff_size, 0); //początkowy stan pamięci - F = Front, R = Rear, S = Size

    if (fork() == 0) { //fork na proces obsługujący producentów
        for (int i = 0; i < producers; i++) {
            pid_t pid = 0;
            if (fork() == 0) {
                pid = getpid();
                write(fd[1], &pid, sizeof(pid_t)); //dodanie pid-u do potoku
                srand(time(NULL) ^ (getpid()<<16)); //unikatowe ziarno dla każdego producenta
                producer(full, empty, buff_size, buffer_mutex[0], buffer_mutex[1], i, empty_queue, full_queue); //produkuj
                break; //po wyprodukowaniu wyjdź z pętli
            }
        }
        for (int i = 0; i < producers; i++) { //pętla oczekująca na sygnał
            wait(NULL);
            exit(0);
        }
    } else {
        if (fork() == 0) {//fork na proces obsługujący konsumentów
            for (int i = 0; i < consumers; i++) {
                pid_t pid = 0;
                if (fork() == 0) {
                    pid = getpid();
                    write(fd[1], &pid, sizeof(pid_t)); //dodanie pid-u do potoku
                    consumer(full, empty, buff_size, buffer_mutex[0], buffer_mutex[2], i, empty_queue, full_queue); //konsumuj
                    break; //po zakończeniu konsumpcji wyjdź
                }
            }
            for (int i = 0; i < consumers; i++) { //pętla oczekująca na sygnał
                wait(NULL);
                exit(0);
            }
        } else {
                int producers_depleted = 0; //liczba wyczerpanych producentów
                int consumers_depleted = 0; //liczba wyczerpanych konsumentów
                int producers_end = 0; //flaga sygnalizująca wyczerpanie producentów
                int consumers_end = 0; //flaga sygnalizująca wyczerpanie konsumentów
                int end_flag = 0; //flaga sygnalizująca możliwość zakończenia programu
                while (1) {
                    usleep(1000000); //interwał
                    lseek(variables_memory, 0, SEEK_SET); //wskaźnik pamięci współdzielonej na początek
                    if (read(variables_memory, &producers_depleted, 1) < 0 || read(variables_memory, &consumers_depleted, 1) < 0) {
                        perror("Error: unable to read data from shm"); //błąd otwarcia pamięci
                    }
                    lseek(variables_memory, 0, SEEK_SET);
                    if (producers_depleted >= producers && producers_end == 0) { //liczba wyczerpanych producentów jest >= liczbie producentów
                        printf("All producers depleted.\n");
                        producers_end = 1;
                    }
                    if (consumers_depleted >= consumers && consumers_end == 0) { //liczba wyczerpanych konsumentów jest >= liczbie konsumentów
                        printf("%70s All consumers depleted.\n", " ");
                        consumers_end = 1;
                    }
                    //konsumenci już się wyczerpali, ale w magazynie jest jeszcze miejsce
                    if (consumers_end && isEmpty(empty_queue)) {
                        printf("%70s Buffer is full and there are no consumers left.\n", " ");
                        end_flag = 1;
                    }
                    //producenci już się wyczerpali, ale w magazynie jeszcze są dane
                    if (producers_end && isFull(empty_queue)) {
                        printf("Buffer is empty and there are no producers left.\n");
                        end_flag = 1;
                    }
                    //koniec lub producenci i konsumenci wyczerpani
                    if (end_flag || (producers_end && consumers_end)) {
                        sleep(5); //oczekiwanie na zakończenie wszystkich procesów potomnych
                        if (VERBOSE)
                            printf("All pids: ");
                        pid_t p = 0;
                        for (int i = 0; i < producers + consumers; i++) {
                            read(fd[0], &p, sizeof(pid_t));
                            if (VERBOSE)
                                printf("%d ", p);
                            if (kill(p, 0) == 0) //sprawdza czy proces o takim pidzie istnieje
                                kill(p, SIGKILL); //zabicie
                            //zabijani są tylko ci producenci i konsumenci, którzy na tym etapie
                            //wciąż oczekują na przyznanie indeksu
                            //(żeby przypadkiem nie zapić jakiegoś innego procesu)
                        }
                        break;
                    }
                }
                //zamknięcie semaforów i pamięci współdzielonej
                destroySemaphore(empty, "/empty_sem");
                destroySemaphore(full, "/full_sem");
                for (int i = 0; i < 3; i++)
                    pthread_mutex_destroy(buffer_mutex[i]);
                for (int i = 0; i < 4; i++) {
                    char mutex_name[15];
                    snprintf(mutex_name, 15, "/mutex_shm_%d", i);
                    shm_unlink(mutex_name);
                }
                shm_unlink("/var_shm");
                shm_unlink("/buffer");
                munmap(ptr_var, 2);
                munmap(ptr_buffer, buff_size * 3);
                free(empty_queue);
                free(full_queue);
                close(fd[0]);
                close(fd[1]);
                //exit(0);
        }
    }

    return 0;
}
