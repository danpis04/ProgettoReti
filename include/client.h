#ifndef CLIENT_H
#define CLIENT_H

#include "common.h"

typedef int (*ClientCallback)(void *);

struct Client {
    int server_socket;
    struct sockaddr_in server_addr;
    fd_set master_set;
    int max_fd;
    bool stdin_enabled;
    ClientCallback server_message_callback;
    ClientCallback stdin_message_callback;
};

struct ClientConfig {
    int server_ip;
    in_port_t server_port;
    ClientCallback server_message_callback;
    ClientCallback stdin_message_callback;
};

struct Client *client_create(struct ClientConfig config);
int client_connect_to_server(struct Client *client);
int client_listen(struct Client *client);
void client_destroy(struct Client *client);

#endif
