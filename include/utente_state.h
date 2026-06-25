#ifndef UTENTE_STATE_H
#define UTENTE_STATE_H

#include "common.h"

// Stati principali del client utente.
enum UtenteState {
    STATE_STARTING_P2P,     // Avvio del server P2P locale.
    STATE_CONNECTING,       // Connessione alla lavagna centrale.
    STATE_IDLE,             // Nessuna card in lavorazione.
    STATE_CHOOSING,         // Elezione P2P in corso.
    STATE_ACK_PENDING,      // L'utente ha vinto e deve inviare ACK_CARD.
    STATE_WORKING,          // Worker attivo sulla card assegnata.
    STATE_DONE_PENDING,     // Worker concluso, CARD_DONE da inviare.
    STATE_DISCONNECTING,    // Chiusura richiesta dall'utente.
    STATE_SHUTTING_DOWN     // Chiusura richiesta da lavagna o P2P.
};

// Proposta ricevuta durante l'elezione P2P.
struct Choice {
    bool used;
    in_port_t port;
    int cost;
};

// Stato globale dell'utente, protetto da mutex.
struct Utente {
    in_port_t port;
    enum UtenteState state;
    pthread_mutex_t mutex;
    pthread_t p2p_thread;
    pthread_t worker_thread;
    bool worker_started;
    int card_id;
    char card_text[CARD_TEXT_SIZE];
    int total_users;
    in_port_t peer_ports[MAX_USERS];
    int peer_count;
    struct Choice choices[MAX_USERS];
    int choice_count;
    int own_cost;
};

// Inizializzazione, stato corrente e dati base dell'utente.
void utente_init(in_port_t port);
void utente_cleanup(void);
in_port_t utente_get_port(void);
enum UtenteState utente_get_state(void);
void utente_update_state(enum UtenteState state);
int utente_get_card_id(void);
int utente_get_own_cost(void);

// Gestione dell'elezione P2P per assegnare una card.
void utente_start_election(int card_id, const char *text, int total_users,
                           const in_port_t *peers, int peer_count, int own_cost);
void utente_record_choice(int card_id, in_port_t port, int cost);

// Estrazione delle azioni pendenti verso la lavagna.
bool utente_take_ack_action(int *card_id);
bool utente_take_manual_ack_action(int requested_card_id, int *card_id);
void utente_mark_working(void);
void utente_mark_done_pending(int card_id);
bool utente_take_done_action(int *card_id);
bool utente_take_manual_done_action(int requested_card_id, int *card_id);
int utente_copy_peers(in_port_t *ports, int max_ports);

// Gestione dei thread ausiliari dell'utente.
int utente_start_p2p(void *(*thread_function)(void *));
void utente_wait_for_p2p_shutdown(void);
int utente_start_worker(void *(*thread_function)(void *));
void utente_wait_for_worker_shutdown(void);

#endif
