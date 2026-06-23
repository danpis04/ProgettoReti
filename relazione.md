# Relazione progetto Reti Informatiche

## Scelte di comunicazione

L'applicazione usa TCP su `127.0.0.1`: la lavagna ascolta sulla porta `5678`, mentre ogni utente avvia un piccolo server P2P sulla propria porta, a partire da `5679`. TCP e' stato scelto per mantenere ordinati e affidabili i messaggi che cambiano lo stato condiviso della kanban; in caso contrario bisognerebbe introdurre conferme applicative e ritrasmissioni, aumentando la complessita' senza un vantaggio concreto per un'applicazione eseguita sullo stesso host.

Ogni messaggio ha un header binario fisso di 8 byte: tipo del comando e lunghezza del payload, entrambi a 32 bit in network byte order. Il payload resta variabile: per esempio `HELLO` contiene la porta dell'utente, `AVAILABLE_CARD` contiene id card, numero utenti, lista peer e testo, mentre `CHOOSE_USER` contiene id card, porta del mittente e costo. Questa scelta evita ambiguita' sulla quantita' di byte da leggere dal socket e resta semplice da ispezionare nel codice.

## Flusso per matricola pari

All'avvio la lavagna crea 10 card in colonna `To Do` e stampa lo stato. Ogni utente, appena eseguito con `./utente <porta>`, registra la propria presenza con `HELLO`. Quando ci sono almeno due utenti e nessuna negoziazione aperta, la lavagna invia a tutti `AVAILABLE_CARD` per la prima card `To Do`, includendo per ogni destinatario la lista degli altri utenti.

Alla ricezione di `AVAILABLE_CARD`, ciascun utente genera un costo con `rand()`, lo registra localmente e invia `CHOOSE_USER` ai peer indicati dalla lavagna. Quando un utente ha raccolto tutti i costi, sceglie il costo minore; in caso di parita' vince la porta piu' bassa. Solo il vincitore invia `ACK_CARD`, quindi la lavagna sposta la card in `Doing`. Il worker dell'utente simula l'esecuzione con `sleep()` e poi invia `CARD_DONE`, facendo passare la card in `Done`. A quel punto la lavagna puo' offrire la card successiva.

## Struttura e gestione dello stato

Il codice e' diviso in moduli: `protocol` gestisce serializzazione e input da terminale, `server` e `client` incapsulano socket e `select()`, `database` mantiene utenti e card, `lavagna_utils` raccoglie le operazioni della lavagna, mentre i file in `src/utente` gestiscono stato condiviso, P2P e worker. L'utente usa un mutex per proteggere la macchina a stati perche' thread principale, server P2P e worker possono aggiornare la stessa elezione.

Pregio principale: il formato a header fisso rende robuste le letture e permette di aggiungere payload diversi senza cambiare la logica dei socket. Inoltre la lavagna mantiene un'unica negoziazione aperta, evitando che un utente vinca piu' card contemporaneamente. Il limite e' che l'elaborazione delle card e' volutamente seriale: e' piu' leggibile e aderente al flusso richiesto, ma non sfrutta al massimo la possibilita' di lavorare su molte card in parallelo. Un altro limite e' l'uso di array a dimensione fissa per utenti e card; per il numero richiesto e' sufficiente e semplice, ma una versione produttiva richiederebbe crescita dinamica.

## Compilazione ed esecuzione

Compilazione:

```sh
make
```

Pulizia dei file generati:

```sh
make clean
```

Esecuzione della lavagna:

```sh
./lavagna
```

Esecuzione di quattro utenti:

```sh
./utente 5679
./utente 5680
./utente 5681
./utente 5682
```

Comandi utili da terminale: `SHOW_LAVAGNA`, `SHOW_UTENTI`, `CREATE_CARD <id> <TODO|DOING|DONE> <testo>`, `MOVE_CARD <id> <TODO|DOING|DONE> [porta]`, `PING_USER <porta>`, `SEND_USER_LIST <porta>` e `QUIT`. Dagli utenti si possono inviare `CREATE_CARD`, `SHOW_LAVAGNA`, `SEND_USER_LIST`, `HELLO` e `QUIT`.
