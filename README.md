# Projekt na laboratoria z PSIW

### Problem producenta-konsumenta na wielu procesach.

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
 * warunkowe skończenie programu wykrywające wyczerpanych producentów konsumentów nie
 * wykrywa pustego/pełnego potoku
 * Program obsługuje sygnał SIGINT (uwaga - obsługa usuwa pliki z semaforami i z pamięcią)
