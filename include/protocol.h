#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "common.h"

// Valore speciale usato dalle callback quando stdin viene chiuso.
#define CALLBACK_REMOVE_STDIN -2

// Tipi di messaggi scambiati tra lavagna, utenti e peer P2P.
enum MessageType {
    MSG_ERR,                // Messaggio non valido, non viene inviato.

    MSG_HELLO,              // Registrazione dell'utente presso la lavagna.
    MSG_QUIT,               // Disconnessione richiesta o notificata.
    MSG_CREATE_CARD,        // Creazione di una nuova card.
    MSG_MOVE_CARD,          // Spostamento manuale di una card.
    MSG_SHOW_LAVAGNA,       // Stampa dello stato della lavagna.
    MSG_SHOW_UTENTI,        // Stampa degli utenti registrati.
    MSG_SEND_USER_LIST,     // Invio della lista utenti a un client.
    MSG_PING_USER,          // Verifica di raggiungibilita di un utente attivo.
    MSG_PONG_LAVAGNA,       // Risposta dell'utente al ping della lavagna.

    MSG_AVAILABLE_CARD,     // Offerta di una card agli utenti disponibili.
    MSG_CHOOSE_USER,        // Costo proposto da un peer per l'elezione.
    MSG_ACK_CARD,           // Conferma di presa in carico della card.
    MSG_CARD_DONE,          // Segnalazione di completamento della card.

    NUM_MSG_TYPES
};

// Messaggio binario: header fisso piu payload opzionale.
struct Message {
    uint32_t type;
    uint32_t payload_length;
    void *payload;
};

// Invio/ricezione su socket secondo il formato comune del protocollo.
ssize_t send_message(int socket_fd, const struct Message *msg);
ssize_t receive_message(int socket_fd, struct Message *msg);

// Conversione tra input testuale da terminale e messaggio.
ssize_t get_command_line_input(struct Message *msg);
const char *message_type_to_string(uint32_t type);

// Utility per serializzare interi a 32 bit in network byte order.
void write_u32(char *buffer, size_t *offset, uint32_t value);
uint32_t read_u32(const char *buffer, size_t *offset);

#endif
