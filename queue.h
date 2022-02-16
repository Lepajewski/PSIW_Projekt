#ifndef QUEUE_H
#define QUEUE_H

typedef struct Queue {
    int front, rear, size, capacity, fd;
    int min_idx, max_idx;
    pthread_mutex_t *lseek_mutex;
} Queue;


Queue* initQueue(int capacity, int fd, int min_idx, int max_idx, pthread_mutex_t *mutex_fd) {
    Queue* queue = (Queue*) malloc(sizeof(Queue)); //alokacja pamięci
    queue->capacity = capacity; //pojemność kolejki
    queue->front = max_idx - 2; //indeks, na którym kolejka przechowuje indeks początkowego elementu
    queue->rear = max_idx - 1; //indeks, na którym kolejka przechowuje indeks końcowego elementu
    queue->size = max_idx; //indeks, na którym kolejka przechowuje rozmiar kolejki
    queue->min_idx = min_idx; //minimalny indeks, na którym operuje kolejka
    queue->max_idx = max_idx; //maksymalny indeks, na którym operuje kolejka
    queue->fd = fd; //deskryptor pamięci współdzielonej - bufora
    queue->lseek_mutex = mutex_fd; //deskryptor muteksa
    capacity = min_idx + capacity - 1; //początkowy indeks ostatniego elementu

    lseek(queue->fd, queue->rear, SEEK_SET);
    write(queue->fd, &capacity, 1);

    lseek(queue->fd, queue->front, SEEK_SET);
    write(queue->fd, &min_idx, 1);

    return queue;
}

int isEmpty(Queue* queue) {
    //zwraca czy kolejka jest pusta
    int size = 0;

    pthread_mutex_lock(queue->lseek_mutex);

    lseek(queue->fd, queue->size, SEEK_SET);
    read(queue->fd, &size, 1);

    pthread_mutex_unlock(queue->lseek_mutex);

    return (size == 0);
}

int isFull(Queue* queue) {
    //zwraca czy kolejka jest pełna
    int size = 0;
    pthread_mutex_lock(queue->lseek_mutex);

    lseek(queue->fd, queue->size, SEEK_SET);
    read(queue->fd, &size, 1);

    pthread_mutex_unlock(queue->lseek_mutex);

    return (queue->capacity == size);
}

void enqueue(Queue* queue, int item) {
    //dodanie elementu do kolejki - operacja musi być atomowa
    if (isFull(queue))
        return;

    int rear = 0, size = 0;

    pthread_mutex_lock(queue->lseek_mutex);

    lseek(queue->fd, queue->rear, SEEK_SET);
    read(queue->fd, &rear, 1);

    //aktualizacja indeksu ostatniego elementu
    rear = (rear - queue->min_idx + 1) % queue->capacity + queue->min_idx;

    lseek(queue->fd, rear, SEEK_SET);
    write(queue->fd, &item, 1);

    lseek(queue->fd, queue->rear, SEEK_SET);
    write(queue->fd, &rear, 1);

    lseek(queue->fd, queue->size, SEEK_SET);
    read(queue->fd, &size, 1);
    size = size + 1;
    lseek(queue->fd, queue->size, SEEK_SET);
    write(queue->fd, &size, 1);

    pthread_mutex_unlock(queue->lseek_mutex);
}

int dequeue(Queue *queue) {
    //usunięcie elementu z kolejki
    if (isEmpty(queue))
        return -1;

    int item = 0, front = 0, size = 0;

    pthread_mutex_lock(queue->lseek_mutex);

    lseek(queue->fd, queue->front, SEEK_SET);
    read(queue->fd, &front, 1);

    lseek(queue->fd, front, SEEK_SET);
    read(queue->fd, &item, 1);

    //aktualizacja indeksu pierwszego elementu
    front = (front - queue->min_idx + 1) % queue->capacity + queue->min_idx;

    lseek(queue->fd, queue->front, SEEK_SET);
    write(queue->fd, &front, 1);

    lseek(queue->fd, queue->size, SEEK_SET);
    read(queue->fd, &size, 1);
    size = size - 1;
    lseek(queue->fd, queue->size, SEEK_SET);
    write(queue->fd, &size, 1);

    pthread_mutex_unlock(queue->lseek_mutex);

    return item;
}

int front(Queue *queue) {
    //zwraca indeks pierwszego elementu kolejki
    if (isEmpty(queue))
        return -1;

    int front = 0;

    pthread_mutex_lock(queue->lseek_mutex);

    lseek(queue->fd, queue->front, SEEK_SET);
    read(queue->fd, &front, 1);

    pthread_mutex_unlock(queue->lseek_mutex);

    return front;
}

int rear(Queue* queue) {
    //zwraca indeks ostatniego elementu kolejki
    if (isEmpty(queue))
        return -1;

    int rear = 0;

    pthread_mutex_lock(queue->lseek_mutex);

    lseek(queue->fd, queue->rear, SEEK_SET);
    read(queue->fd, &rear, 1);

    pthread_mutex_unlock(queue->lseek_mutex);

    return rear;
}


#endif
