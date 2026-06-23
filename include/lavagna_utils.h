#ifndef LAVAGNA_UTILS_H
#define LAVAGNA_UTILS_H

#include "common.h"
#include "server.h"

int send_user_list(int socket_fd, in_port_t excluded_port);
int send_user_ping(int socket_fd);
int send_user_quit(int socket_fd);
int send_available_card(struct Server *server);
int create_card_from_payload(const char *payload);
int move_card_from_payload(const char *payload);
void disconnect_user(struct Server *server, in_port_t user_port);
void check_timeout(struct Server *server);

#endif
