# Documentazione Progetto di Reti Informatiche A.A. 2025/2026

## Scelte implementative sulla parte di comunicazione

La comunicazione tra lavagna e utenti usa esclusivamente TCP su `127.0.0.1`. La lavagna ascolta sulla porta `5678`; gli utenti usano porte incrementali da `5679` e possono essere avviati sia con `./utente <porta>`, come indicato dalla specifica, sia con `./utente`, nel qual caso viene scelta la prima porta libera. TCP e' stato preferito per evitare perdita o riordino dei messaggi che modificano lo stato della kanban: registrazione, assegnazione, completamento e controllo di presenza devono essere osservati nello stesso ordine dai partecipanti.

Anche lo scambio tra utenti per `CHOOSE_USER` usa TCP. Una soluzione UDP sarebbe piu' leggera, ma richiederebbe ritrasmissioni o timeout applicativi per non bloccare la convergenza quando un costo non arriva. Considerato che tutte le componenti girano sullo stesso host e che il numero di utenti e' limitato, l'affidabilita' del TCP semplifica il protocollo e rende piu' chiaro il comportamento.

Ogni messaggio ha un header binario fisso di 8 byte: tipo del comando e lunghezza del payload, entrambi a 32 bit in network byte order. Il payload e' binario per id, porte e contatori, mentre le descrizioni delle card restano stringhe C terminate da `\0`. Questa struttura rispetta il requisito di sapere quanti byte leggere dal socket e mantiene compatta la codifica dei messaggi piu' frequenti (`HELLO`, `ACK_CARD`, `CARD_DONE`, `PING_USER`, `PONG_LAVAGNA`).

Le connessioni multiple della lavagna sono gestite con `select()`. L'utente usa un approccio misto: il thread principale mantiene la connessione con la lavagna e legge lo standard input, un thread separato ascolta i messaggi P2P sulla porta dell'utente, e un worker simula l'esecuzione della card. Questa scelta evita di bloccare la risposta a `PING_USER` o a `CHOOSE_USER` mentre l'utente sta lavorando.

## Struttura logica del programma

Il codice e' organizzato in moduli separati per responsabilita': `server` e `client` astraggono socket e multiplexing, `protocol` serializza e deserializza i messaggi, `database` mantiene utenti e card della lavagna, `lavagna_utils` contiene le operazioni applicative della lavagna, mentre `utente_state`, `p2p_utils` e i thread dell'utente gestiscono elezione e lavoro.

La lavagna inizializza almeno 10 card in `To Do` e stampa lo stato all'avvio. Quando riceve `HELLO`, registra la porta dell'utente e aggiorna il contatore. Appena sono presenti almeno due utenti e una card disponibile, invia `AVAILABLE_CARD` a tutti gli utenti registrati, includendo la prima card in `To Do`, il numero totale di utenti e la lista dei peer escluso il destinatario.

Per matricola pari, ogni utente genera il costo con:

```c
srand(time(NULL));
int n = rand();
```

Il seed viene inizializzato una sola volta all'avvio dell'utente; a ogni `AVAILABLE_CARD` viene poi chiamato `rand()` per ottenere il costo della card. Per evitare che utenti avviati nello stesso secondo producano sempre il primo valore identico, la sequenza viene avanzata in base alla porta dell'utente. Il costo viene inviato agli altri utenti con `CHOOSE_USER`. Quando un utente ha raccolto tutti i costi, sceglie il valore minore; in caso di parita' vince la porta piu' bassa. Solo il vincitore invia `ACK_CARD`, la lavagna sposta la card in `Doing`, il worker dell'utente simula l'esecuzione con `sleep()` e infine invia `CARD_DONE`, facendo passare la card in `Done`.

Le strutture dati sono array a dimensione fissa per utenti e card. Il pregio e' la semplicita': gli accessi sono lineari ma leggibili, e il limite massimo e' esplicito. Il difetto e' che una versione produttiva dovrebbe usare contenitori dinamici o liste per evitare un tetto compilato nel programma. Per l'ambito del progetto, 50 utenti e 128 card sono sufficienti a coprire gli scenari previsti mantenendo il codice compatto.

Lo stato dell'utente e' condiviso tra thread e protetto da mutex. Questa scelta riduce il rischio di race tra ricezione di `AVAILABLE_CARD`, ricezione dei costi P2P e completamento del worker. Il costo principale e' una maggiore attenzione nella transizione tra stati (`IDLE`, `CHOOSING`, `ACK_PENDING`, `WORKING`, `DONE_PENDING`), ma il flusso resta esplicito e facilmente spiegabile.

## Compilazione

Il progetto si compila con:

```sh
make
```

Il Makefile abilita `-Wall -Wextra -Wpedantic -std=c11` e produce gli eseguibili:

```text
lavagna
utente
```

Per avviare la lavagna:

```sh
./lavagna
```

Per avviare un utente con porta esplicita:

```sh
./utente 5679
```

Per lasciare al processo la scelta della prima porta libera:

```sh
./utente
```

La pulizia dei file generati avviene con:

```sh
make clean
```

## Comandi da terminale

La lavagna accetta `SHOW_LAVAGNA`, `SHOW_UTENTI`, `MOVE_CARD <id> <TODO|DOING|DONE> [porta]`, `SEND_USER_LIST <porta>`, `PING_USER <porta>` e `AVAILABLE_CARD`.

Ogni utente accetta `CREATE_CARD <id> <TODO|DOING|DONE> <testo>`, `SHOW_LAVAGNA`, `HELLO`, `CHOOSE_USER [costo]`, `ACK_CARD [id]`, `CARD_DONE [id]`, `PONG_LAVAGNA` e `QUIT`. `CHOOSE_USER`, `ACK_CARD` e `CARD_DONE` da terminale usano la card corrente dell'utente; i parametri opzionali servono a inviare un costo esplicito o a rifiutare invii accidentali su una card diversa.
