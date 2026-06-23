# Progetto Reti Informatiche 2025/2026

Applicazione distribuita in C11 che implementa una lavagna kanban per matricola pari.
La lavagna ascolta su `127.0.0.1:5678`; ogni utente apre un server P2P su una porta da `5679` in poi.

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

## Comandi

Sulla lavagna:

```text
SHOW_LAVAGNA
SHOW_UTENTI
CREATE_CARD <id> <TODO|DOING|DONE> <descrizione>
MOVE_CARD <id> <TODO|DOING|DONE> [porta]
SEND_USER_LIST <porta>
PING_USER <porta>
QUIT
```

Sugli utenti:

```text
CREATE_CARD <id> <TODO|DOING|DONE> <descrizione>
SHOW_LAVAGNA
SEND_USER_LIST
HELLO
ACK_CARD [id]
CARD_DONE [id]
PONG_LAVAGNA
QUIT
```

Con almeno due utenti registrati, la lavagna invia `AVAILABLE_CARD`; gli utenti si scambiano `CHOOSE_USER`, il costo minore vince la card e lo comunica con `ACK_CARD`. Al completamento viene inviato `CARD_DONE`. I comandi `ACK_CARD` e `CARD_DONE` da terminale sono disponibili quando l'utente ha una card corrente.
