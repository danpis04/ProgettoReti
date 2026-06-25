#ifndef UTENTE_UTILS_H
#define UTENTE_UTILS_H

#include "common.h"

// Messaggi inviati dall'utente alla lavagna centrale.
int send_server_hello(int server_fd);
int send_server_ack_card(int server_fd, int card_id);
int send_server_card_done(int server_fd, int card_id);
int send_server_pong(int server_fd);

#endif
