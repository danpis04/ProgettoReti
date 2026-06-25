#ifndef DATABASE_H
#define DATABASE_H

#include "common.h"

// Stato logico di un utente registrato sulla lavagna.
enum UserStatus {
    USER_STATUS_EMPTY,
    USER_STATUS_IDLE,
    USER_STATUS_ACTIVE,
    USER_STATUS_PINGED
};

// Colonne della lavagna Kanban.
enum CardStatus {
    CARD_STATUS_TODO,
    CARD_STATUS_DOING,
    CARD_STATUS_DONE
};

// Utente connesso, identificato dalla porta del server P2P.
struct User {
    bool used;
    int socket_fd;
    in_port_t port;
    enum UserStatus status;
    int assigned_card_id;
    time_t last_ping_timestamp;
};

// Card della lavagna con eventuale utente assegnato.
struct Card {
    bool used;
    int id;
    enum CardStatus status;
    char text[CARD_TEXT_SIZE];
    in_port_t assigned_user_port;
    time_t last_activity_timestamp;
};

// Stato completo della lavagna mantenuto solo dal server centrale.
struct Database {
    int lavagna_id;
    struct User users[MAX_USERS];
    struct Card cards[MAX_CARDS];
    int num_users;
    int num_cards;
    int offered_card_id;
};

// Inizializzazione e rilascio del database.
void database_init(void);
void database_cleanup(void);

// Funzioni sugli utenti registrati.
int database_add_user(int socket_fd, in_port_t port);
int database_remove_user(in_port_t port);
int database_get_num_users(void);
int database_get_users_list(in_port_t **user_ports);
int database_get_users_list_except(in_port_t excluded_port, in_port_t **user_ports);
in_port_t database_get_port_from_socket(int socket_fd);
int database_get_socket_from_port(in_port_t port);
enum UserStatus database_user_get_status(in_port_t port);
int database_user_set_status(in_port_t port, enum UserStatus status);
int database_user_assign_card(in_port_t port, int card_id);
int database_user_clear_card(in_port_t port);
int database_user_set_ping(in_port_t port);
int database_user_clear_ping(in_port_t port);
int database_user_get_timed_out(in_port_t **user_ports, int timeout);
void database_print_users(void);

// Funzioni sulle card e sui passaggi tra colonne.
int database_create_card(int requested_id, enum CardStatus status, const char *text);
int database_get_next_todo_card(void);
int database_get_offered_card(void);
void database_set_offered_card(int card_id);
void database_clear_offered_card(void);
bool database_has_doing_cards(void);
int database_card_get_text(int card_id, char *buffer, size_t buffer_size);
enum CardStatus database_card_get_status(int card_id);
in_port_t database_card_get_user(int card_id);
int database_card_doing(in_port_t port, int card_id);
int database_card_done(in_port_t port, int card_id);
int database_card_todo(in_port_t port);
int database_move_card(int card_id, enum CardStatus status, in_port_t port);
int database_card_reset_timestamp(in_port_t port);
int database_card_get_timed_out(int **card_ids, int timeout);
void database_print_cards(void);

#endif
