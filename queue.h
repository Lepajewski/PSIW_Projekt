#ifndef QUEUE_H
#define QUEUE_H

typedef struct Queue {
    int front, rear, size, capacity;
    int array[MAX_BUFFER_SIZE];
} Queue;


Queue initQueue(int capacity) {
    Queue queue;
    queue.capacity = capacity;
    queue.front = 0;
    queue.size = 0;
    queue.rear = capacity - 1;
    for (int i = 0; i < MAX_BUFFER_SIZE; i++)
        queue.array[i] = 0;

    return queue;
}

int isEmpty(Queue *queue) {
    //zwraca czy kolejka jest pusta
    return (queue->size == 0);
}

int isFull(Queue *queue) {
    //zwraca czy kolejka jest pełna
    return (queue->capacity == queue->size);
}

void enqueue(Queue *queue, int item) {
    //dodanie elementu do kolejki - operacja musi być atomowa
    if (isFull(queue))
        return;

    //aktualizacja indeksu ostatniego elementu
    queue->rear = (queue->rear + 1) % queue->capacity;

    queue->array[queue->rear] = item;

    queue->size = queue->size + 1;
}

int dequeue(Queue *queue) {
    //usunięcie elementu z kolejki
    if (isEmpty(queue))
        return -1;

    int item = queue->array[queue->front];

    //aktualizacja indeksu pierwszego elementu
    queue->front = (queue->front + 1) % queue->capacity;

    queue->size = queue->size - 1;

    return item;
}

int front(Queue *queue) {
    //zwraca indeks pierwszego elementu kolejki
    if (isEmpty(queue))
        return -1;
    return queue->array[queue->front];
}

int rear(Queue *queue) {
    //zwraca indeks ostatniego elementu kolejki
    if (isEmpty(queue))
        return -1;
    return queue->array[queue->rear];
}

#endif
