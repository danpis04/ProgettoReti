#ifndef SERVER_H
#define SERVER_H

#include "common.h"

typedef int (*ServerCallback)(void *);

struct Server {
    int socket_fd;
    struct sockaddr_in addr;
    fd_set master_set;
    int max_fd;
    bool stdin_enabled;
    ServerCallback client_message_callback;
    ServerCallback stdin_message_callback;
};

struct ServerConfig {
    in_port_t port;
    ServerCallback client_message_callback;
    ServerCallback stdin_message_callback;
};

struct Server *server_create(struct ServerConfig config);
int server_init(struct Server *server);
int server_run(struct Server *server);
int server_remove_client(struct Server *server, int fd);
void server_shutdown(struct Server *server);

#endif
