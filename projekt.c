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

#define DEFAULT_BUFFER_SIZE 5 //domyślny rozmiar bufora - "magazynu"
#define MAX_BUFFER_SIZE 100 //maksymalny rozmiar bufora
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

typedef struct Buffer {
    Queue empty_queue;
    Queue full_queue;
    int items[MAX_BUFFER_SIZE];
} Buffer;

int variables_memory = 0; //deskryptor pamięci współdzielonej dla zmiennych przechowujących ilość wyczerpanych producentów i konsumentów
int buffer_memory = 0; //deskryptor pamięci współdzielonej dla bufora
int *ptr_var = NULL; //wskaźnik na zmapowaną pamięć współdzieloną zmiennych
int fd[2]; //deskryptory dla potoku, który zawiera PID-y procesów potomnych
void print_shm(int flag, Buffer *b);

int buff_size = DEFAULT_BUFFER_SIZE; //rozmiar bufora
int producers = DEFAULT_PRODUCERS_NUMBER; //liczba producentów
int consumers = DEFAULT_CONSUMERS_NUMBER; //liczba konsumentów

sem_t *empty_sem = NULL; //semafor wolny
sem_t *full_sem = NULL; //semafor pełny
sem_t *consumers_sem = NULL; //semafor konsumentów
sem_t *producers_sem = NULL; //semafor producentów
sem_t *memory_sem = NULL; //semafor wypisywania pamięci

Buffer *b = NULL; //bufor
void *b_m = NULL;

void producer(int id, Buffer *b) {
    int a = 0, item = 0;
    for (int i= 0; i < MAX_PRODUCTS_PRODUCED; i++) {
        sem_wait(empty_sem); //opuszczenie semafora pustego - sygnalizacja dla konsumentów że coś jest w magazynie

        sem_wait(producers_sem); //wejście do sekcji producentów (tylko jeden producent naraz może uzyskać indeks)
        a = dequeue(&b->empty_queue);//zdjęcie indeksu z kolejki wolnych indeksów
        sem_post(producers_sem);

        item = rand() % (MAX_ITEM_NUMBER - MIN_ITEM_NUMBER + 1) + MIN_ITEM_NUMBER; //losowy przedmiot
        usleep(1000 * (rand() % (MAX_PRODUCING_TIME_MS - MIN_PRODUCING_TIME_MS + 1) + MIN_PRODUCING_TIME_MS)); //czasochłonna produkcja
        printf("Producer %2d: produces item %3d produced items: %3d/%d | PID: %5d\n", id, item, i+1, MAX_PRODUCTS_PRODUCED, getpid());


        sem_wait(consumers_sem); //sekcja dostępu do kolejki indeksów zajętych

        enqueue(&b->full_queue, a); //aktualizacja kolejki indeksów zajętych - dodanie do niej przyznanego indeksu
        b->items[a] = item;//dodanie przedmiotu na otrzymanym indeksie

        if (VERBOSE) {
            sem_wait(memory_sem); //wejście do sekcji wypisującej zawartość pamięci
            //wyświetlenie pamięci
            print_shm(0, b);
            sem_post(memory_sem);
        }

        sem_post(consumers_sem);

        sem_post(full_sem); //podniesienie semafora pełnego
    }
    if (VERBOSE) { //producent wyczerpany
        printf("PRODUCER %2d DEPLETED\n", id);
    }
    if (kill(getpid(), SIGUSR1) == -1) { //wysyła sygnał
        exit(1);
    }
}

void consumer(int id, Buffer *b) {
    int item = 0, a = 0;
    for (int i = 0; i < MAX_PRODUCTS_CONSUMED; i++) {
        sem_wait(full_sem); //opuszczenie semafora pełny - zmniejszenie jego wartości o 1

        sem_wait(consumers_sem); //sekcja krytyczna dla konsumentów ubiegających się o przyznanie indeksu

        a = dequeue(&b->full_queue); //zdjęcie indeksu z kolejki zajętych indeksów
        item = b->items[a]; //pobranie produktu

        sem_post(consumers_sem);

        printf("%70s Consumer %2d: consumes item: %3d consumed items: %3d/%d | PID: %5d\n", " ", id, item, i+1, MAX_PRODUCTS_CONSUMED, getpid());
        usleep(1000 * (rand() % (MAX_CONSUMING_TIME_MS - MIN_CONSUMING_TIME_MS + 1) + MIN_CONSUMING_TIME_MS)); //czasochłonna konsumpcja

        sem_wait(producers_sem); //sekcja dostępu do kolejki indeksów wolnych

        enqueue(&b->empty_queue, a); //aktualizacja kolejki indeksów wolnych - dodanie do niej przyznanego indeksu

        if (VERBOSE) {
            sem_wait(memory_sem);
            print_shm(1, b);
            sem_post(memory_sem);
        }

        sem_post(producers_sem);

        sem_post(empty_sem);
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

void destroyShm(char *name, int descriptor, int size) {
    shm_unlink(name);
    munmap(&descriptor, size);
}

void *mapShm(int size, int fd) {
    return mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
}

void closeAllUtilities() {
    //w przypadku sygnału SIGINT usuwa semafory, pamięć współdzieloną, potok i kolejki
    destroySemaphore(empty_sem, "/empty_sem");
    destroySemaphore(full_sem, "/full_sem");
    destroySemaphore(producers_sem, "/producers_sem");
    destroySemaphore(consumers_sem, "/consumers_sem");
    destroySemaphore(memory_sem, "/memory_sem");
    destroyShm("/var_shm", *ptr_var, 2);
    shm_unlink("/buffer_shm");
    munmap(b_m, sizeof(Buffer));
    free(b);
    free(b_m);
    close(fd[0]);
    close(fd[1]);
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

void print_shm(int flag, Buffer *b) {
    if (flag) printf("%70s ", " ");
    printf("Free indexes: \n");
    if (flag) printf("%70s ", " ");
    for (int i = 0; i < b->empty_queue.capacity; i++)
        printf("%3d ", i);
    printf("  F   R   S\n");
    if (flag) printf("%70s ", " ");
    for (int i = 0; i < b->empty_queue.capacity; i++) {
        printf("%3d ", b->empty_queue.array[i]);
    }
    printf("%3d %3d %3d\n", b->empty_queue.front, b->empty_queue.rear, b->empty_queue.size);

    if (flag) printf("%70s ", " ");
    printf("Occupied indexes: \n");
    if (flag) printf("%70s ", " ");
    for (int i = 0; i < b->full_queue.capacity; i++)
        printf("%3d ", i);
    printf("  F   R   S\n");
    if (flag) printf("%70s ", " ");
    for (int i = 0; i < b->full_queue.capacity; i++) {
        printf("%3d ", b->full_queue.array[i]);
    }
    printf("%3d %3d %3d\n", b->full_queue.front, b->full_queue.rear, b->full_queue.size);

    if (flag) printf("%70s ", " ");
    printf("Products:\n");
    if (flag) printf("%70s ", " ");
    for (int i = 0; i < b->full_queue.capacity; i++) {
        printf("%3d ", b->items[i]);
    }
    printf("\n\n");
}

int main(int argc, char *argv[])
{
    srand(time(NULL) ^ (getpid()<<16)); //unikatowe ziarno
    signal(SIGINT, closeAllUtilities); //zmiana obsługi sygnałów
    signal(SIGUSR1, detectProducersDepletion);
    signal(SIGUSR2, detectConsumersDepletion);

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

    if (consumers <= 0 || producers <= 0 || buff_size <= 0 || buff_size > MAX_BUFFER_SIZE) { //nieprawidłowe argumenty
        perror("Error: Invalid arguments");
        exit(-1);
    }

    empty_sem = initSemaphore("/empty_sem", buff_size); //semafor "pusty"
    full_sem = initSemaphore("/full_sem", 0); //semafor "pełny"
    producers_sem = initSemaphore("/producers_sem", 1); //semafor binarny dla producentów
    consumers_sem = initSemaphore("/consumers_sem", 1); //semafor binarny dla konsumentów
    memory_sem = initSemaphore("/memory_sem", 1); //semafor dla pamięci współdzielonej

    variables_memory = initShm("/var_shm", 2); //otwarcie pliku pamięci współdzielonej dla zmiennych
    ptr_var = mapShm(2, variables_memory); //zmapowanie bloku pamięci współdzielonej

    buffer_memory = shm_open("/buffer_shm", O_CREAT | O_RDWR, 0); //otwarcie pliku pamięci współdzielonej dla bufora
    ftruncate(buffer_memory, sizeof(Buffer));

    b_m = mmap(NULL, sizeof(Buffer), PROT_READ|PROT_WRITE, MAP_SHARED, buffer_memory, 0); //zmapowanie pamięci

    if (ptr_var == MAP_FAILED || b_m == MAP_FAILED) { //błąd mapowania
        perror("Error: shared memory map failed");
        exit(-1);
    }

    Buffer *b = b_m;
    //kolejka indeksów wolnych
    b->empty_queue = initQueue(buff_size);
    //kolejka indeksów zajętych
    b->full_queue = initQueue(buff_size);
    //tablica produktów
    for (int i = 0; i < MAX_BUFFER_SIZE; i++)
        b->items[i] = 0;

    pipe(fd); //ma to na celu uniknięcie zawieszonych procesów po zakończeniu głównego procesu
    int pipe_size = fcntl(fd[1], F_SETPIPE_SZ, producers + consumers); //ustawianie rozmiaru potoku na zadany
    if (pipe_size == -1) {
        perror("Error: setting pipe size");
        exit(-1);
    }

    for (int i = 0; i < buff_size; i++) {
        enqueue(&b->empty_queue, i); //dodanie wolnych indeksów do kolejki wolnych indeksów
    }

    if (VERBOSE)
        print_shm(0, b); //początkowy stan pamięci - F = Front, R = Rear, S = Size

    if (fork() == 0) { //fork na proces obsługujący producentów
        for (int i = 0; i < producers; i++) {
            pid_t pid = 0;
            if (fork() == 0) {
                pid = getpid();
                write(fd[1], &pid, sizeof(pid_t)); //dodanie pid-u do potoku
                srand(time(NULL) ^ (getpid()<<16)); //unikatowe ziarno dla każdego producenta
                producer(i, b); //produkuj
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
                    consumer(i, b); //konsumuj
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
                if (consumers_end && isEmpty(&b->empty_queue)) {
                    printf("%70s Buffer is full and there are no consumers left.\n", " ");
                    end_flag = 1;
                }
                //producenci już się wyczerpali, ale w magazynie jeszcze są dane
                if (producers_end && isFull(&b->empty_queue)) {
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
                    }
                    break;
                }
            }
            //zamknięcie semaforów i pamięci współdzielonej
            closeAllUtilities();
            //exit(0);
        }
    }
    return 0;
}
