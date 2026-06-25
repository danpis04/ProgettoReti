# Progetto Reti Informatiche 2025/2026

## Compilazione

```sh
make
```

Il Makefile usa:

```make
CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic -std=c11
```

Per rimuovere eseguibili e file oggetto:

```sh
make clean
```

## Esecuzione

Avviare prima la lavagna:

```sh
./lavagna
```

Avviare poi gli utenti. La forma richiesta dalla specifica e':

```sh
./utente 5679
./utente 5680
./utente 5681
./utente 5682
```

E' disponibile anche l'avvio senza parametro:

```sh
./utente
```

In questo caso il processo sceglie la prima porta libera a partire da `5679`.
