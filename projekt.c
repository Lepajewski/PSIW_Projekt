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
 * oraz liczby maksymalnych produktów do produkcji/konsumpcji
 * warunkowe skończenie programu wykrywające wyczerpanych producentów lub konsumentów
 * wykrywa pusty/pełny potok przy zakończeniu się programu
 * Program obsługuje sygnał SIGINT (uwaga - obsługa usuwa pliki z semaforami i z pamięcią)
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

#define DEFAULT_BUFFER_SIZE 5 //domyślny rozmiar bufora - "magazynu"
#define DEFAULT_PRODUCERS_NUMBER 1 //domyślna liczba producentów
#define DEFAULT_CONSUMERS_NUMBER 1 //domyślna liczba konsumentów
#define MIN_ITEM_NUMBER 0 //minimalny nr wyprodukowanego przedmiotu
#define MAX_ITEM_NUMBER 100 //maksymalny nr wyprodukowanego przedmiotu
#define MIN_PRODUCING_TIME_MS 500 //minimalny czas produkcji
#define MAX_PRODUCING_TIME_MS 1500 //maksymalny czas produkcji
#define MAX_PRODUCTS_PRODUCED 10 //maksymalna liczba produktów możliwa do wyprodukowania przez jednego producenta
#define MIN_CONSUMING_TIME_MS 500 //minimalny czas konsumpcji
#define MAX_CONSUMING_TIME_MS 1500 //maksymalny czas konsumpcji
#define MAX_PRODUCTS_CONSUMED 5 //maksymalna liczba produktów możliwa do konsumpcji przez jednego konsumenta
#define VERBOSE 1 //flaga, która ustawia powiadomienia o zakończonych producentach i konsumentach oraz o ich liczbie
int variables_memory = 0; //globalna zmienna - deskryptor pamięci współdzielonej
char *ptr = NULL; //wskaźnik na zmapowaną pamięć współdzieloną

void producer(sem_t *full, sem_t *empty, int buff_size, pthread_mutex_t buffer_mutex, int fd[2], int id) {
    for (int i= 0; i < MAX_PRODUCTS_PRODUCED; i++) {
        int item = rand() % (MAX_ITEM_NUMBER - MIN_ITEM_NUMBER + 1) + MIN_ITEM_NUMBER;
        usleep(1000 * (rand() % (MAX_PRODUCING_TIME_MS - MIN_PRODUCING_TIME_MS + 1) + MIN_PRODUCING_TIME_MS));

        sem_wait(empty); //opuszczenie semafora pustego
        pthread_mutex_lock(&buffer_mutex); //wejście do sekcji krytycznej

        write(fd[1], &item, sizeof(item)); //zapisuje produkt w potoku
        printf("Producer %2d: produces item %3d produced items: %3d/%d | PID: %5d\n", id, item, i+1, MAX_PRODUCTS_PRODUCED, getpid());

        pthread_mutex_unlock(&buffer_mutex); //wyjście z sekcji krytycznej
        sem_post(full); //podniesienie semafora pełnego
    }
    if (VERBOSE) { //producent wyczerpany
        printf("PRODUCER %2d DEPLETED\n", id);
    }
    if (kill(getpid(), SIGUSR1) == -1) { //wysyła sygnał
        exit(1);
    }
}

void consumer(sem_t *full, sem_t *empty, int buff_size, pthread_mutex_t buffer_mutex, int fd[2], int id) {
    for (int i = 0; i < MAX_PRODUCTS_CONSUMED; i++) {
        sem_wait(full); //opuszczenie semafora pełny - zmniejszenie jego wartości o 1
        pthread_mutex_lock(&buffer_mutex); //wejście do sekcji krytycznej
        int item = -1;
        read(fd[0], &item, sizeof(item)); //pobranie produktu z potoku
        printf("%70s Consumer %2d: consumes item: %3d consumed items: %3d/%d | PID: %5d\n", " ", id, item, i+1, MAX_PRODUCTS_CONSUMED, getpid());
        usleep(1000 * (rand() % (MAX_CONSUMING_TIME_MS - MIN_CONSUMING_TIME_MS + 1) + MIN_CONSUMING_TIME_MS));

        pthread_mutex_unlock(&buffer_mutex); //wyjście z sekcji krytycznej
        sem_post(empty);
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
    munmap(ptr, 2);
    sem_close(sem);
    sem_unlink(name);
}

void keyboardInterrupt() {
    //w przypadku sygnału SIGINT usuwa pliki z semaforami oraz z pamięcią współdzieloną
    system("rm /dev/shm/sem.empty_sem /dev/shm/sem.full_sem /dev/shm/var_shm 2> nul");
    exit(0);
}

void detectProducersDepletion() {
    //pobiera pierwszy bajt z pamięci współdzielonej jako liczbę wyczerpanych producentów
    int data = 0;
    msync(ptr, 2, MS_SYNC);
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
    msync(ptr, 2, MS_SYNC);
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

int main(int argc, char *argv[])
{
    srand(time(NULL) ^ (getpid()<<16)); //unikatowe ziarno
    signal(SIGINT, keyboardInterrupt); //zmiana obsługi sygnałów
    signal(SIGUSR1, detectProducersDepletion);
    signal(SIGUSR2, detectConsumersDepletion);

    int buff_size = DEFAULT_BUFFER_SIZE; //rozmiar bufora - "magazynu"
    int producers = DEFAULT_PRODUCERS_NUMBER; //liczba producentów
    int consumers = DEFAULT_CONSUMERS_NUMBER; //liczba konsumentów
    int fd[2]; //deskryptory potoku
    pipe(fd); //tworzenie potoku
    int pipe_size = fcntl(fd[1], F_SETPIPE_SZ, buff_size); //ustawianie rozmiaru potoku na zadany
    if (pipe_size == -1) {
        perror("Error: setting buffer size");
        exit(-1);
    }
    if (argc > 4) { //nieprawidłowa liczba argumentów
        printf("Error: invalid number of arguments\n");
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
        printf("Error: Invalid arguments\n");
        exit(-1);
    }

    sem_t *empty = initSemaphore("/empty_sem", buff_size); //semafor "pusty"
    sem_t *full = initSemaphore("/full_sem", 0); //semafor "pełny"
    pthread_mutex_t buffer_mutex; //zamek do potoku
    pthread_mutex_init(&buffer_mutex, NULL); //inicjalizacja zamka
    variables_memory = shm_open("/var_shm", O_CREAT | O_RDWR, 0600); //otwarcie pliku pamięci współdzielonej
    if (variables_memory == -1) { //błąd otwarcia pamięci
        printf("Error: failed to create shared memory for variables");
        exit(-1);
    }
    ftruncate(variables_memory, 2); //ustawienie rozmiaru pamięci na dwa bajty
    ptr = mmap(NULL, 2, PROT_READ|PROT_WRITE, MAP_SHARED, variables_memory, 0); //zmapowanie bloku pamięci współdzielonej
    if (ptr == MAP_FAILED) { //błąd mapowania
        perror("Error: map failed");
        exit(-1);
    }

    if (fork() == 0) { //fork na proces obsługujący producentów
        for (int i = 0; i < producers; i++) {
            if (fork() == 0) {
                srand(time(NULL) ^ (getpid()<<16)); //unikatowe ziarno dla każdego producenta
                producer(full, empty, buff_size, buffer_mutex, fd, i); //produkuj
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
                if (fork() == 0) {
                    consumer(full, empty, buff_size, buffer_mutex, fd, i); //konsumuj
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
                int bytes_available_to_read = 0; //liczba bajtów zapisanych w potoku
                int producers_end = 0; //flaga sygnalizująca wyczerpanie producentów
                int consumers_end = 0; //flaga sygnalizująca wyczerpanie konsumentów
                int end_flag = 0;
                while (1) {
                    usleep(1000000); //interwał
                    lseek(variables_memory, 0, SEEK_SET); //wskaźnik pamięci współdzielonej na początek
                    if (read(variables_memory, &producers_depleted, 1) < 0 || read(variables_memory, &consumers_depleted, 1) < 0) {
                        perror("Error: unable to read data from shm"); //błąd otwarcia pamięci
                    }
                    lseek(variables_memory, 0, SEEK_SET);
                    if (producers_depleted >= producers && producers_end == 0) { //liczba wyczerpanych producentów jest >= liczbie producentów
                        if (VERBOSE) {
                            printf("All producers depleted.\n");
                        }
                        producers_end = 1;
                    }
                    if (consumers_depleted >= consumers && consumers_end == 0) { //liczba wyczerpanych konsumentów jest >= liczbie konsumentów
                        if (VERBOSE) {
                            printf("%70s All consumers depleted.\n", " ");
                        }
                        consumers_end = 1;
                    }
                    if (ioctl(fd[0], FIONREAD, &bytes_available_to_read)) { //sprawdza ile bajtów jest zapisanych w potoku
                            perror("Error: ");
                            exit(-1);
                        }
                    //konsumenci już się wyczerpali, ale w potoku jest jeszcze miejsce
                    if (consumers_end && bytes_available_to_read == buff_size * 4) {
                        if (VERBOSE) {
                            printf("%70s Buffer is full and there are no consumers left.\n", " ");
                        }
                        end_flag = 1;
                    }
                    //producenci już się wyczerpali, ale w potoku jeszcze są dane
                    if (producers_end && bytes_available_to_read == 0) {
                        if (VERBOSE) {
                            printf("Buffer is empty and there are no producers left.\n");
                        }
                        end_flag = 1;
                    }
                    //koniec lub producenci i konsumenci wyczerpani
                    if (end_flag || (producers_end && consumers_end)) {
                        sleep(5); //oczekiwanie na zakończenie wszystkich procesów potomnych
                        break;
                    }
                }
                //zamknięcie semaforów, potoku i pamięci współdzielonej
                destroySemaphore(empty, "/empty_sem");
                destroySemaphore(full, "/full_sem");
                pthread_mutex_destroy(&buffer_mutex);
                shm_unlink("/var_shm");
                munmap(ptr, 2);
                close(fd[0]);
                close(fd[1]);
                //exit(0);
        }
    }

    return 0;
}
