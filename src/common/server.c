#include "../../include/server.h"
#include "../../include/protocol.h"

static int server_add_fd(struct Server *server, int fd) {
    if (fd < 0 || fd >= FD_SETSIZE) {
        return -1;
    }

    FD_SET(fd, &server->master_set);
    if (fd > server->max_fd) {
        server->max_fd = fd;
    }

    return 0;
}

static void server_refresh_max_fd(struct Server *server) {
    while (server->max_fd >= 0 && !FD_ISSET(server->max_fd, &server->master_set)) {
        server->max_fd--;
    }
}

int server_remove_client(struct Server *server, int fd) {
    if (server == NULL || fd < 0 || fd >= FD_SETSIZE || !FD_ISSET(fd, &server->master_set)) {
        return -1;
    }

    FD_CLR(fd, &server->master_set);
    close(fd);
    if (fd == server->max_fd) {
        server_refresh_max_fd(server);
    }

    return 0;
}

static void server_remove_stdin(struct Server *server) {
    FD_CLR(STDIN_FILENO, &server->master_set);
    server->stdin_enabled = false;
    if (server->max_fd == STDIN_FILENO) {
        server_refresh_max_fd(server);
    }
}

struct Server *server_create(struct ServerConfig config) {
    struct Server *server = malloc(sizeof(*server));
    if (server == NULL) {
        return NULL;
    }

    server->socket_fd = -1;
    server->addr.sin_family = AF_INET;
    server->addr.sin_port = htons(config.port);
    server->addr.sin_addr.s_addr = htonl(INADDR_ANY);
    FD_ZERO(&server->master_set);
    server->max_fd = -1;
    server->stdin_enabled = false;
    server->client_message_callback = config.client_message_callback;
    server->stdin_message_callback = config.stdin_message_callback;

    return server;
}

int server_init(struct Server *server) {
    int reuse = 1;

    server->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->socket_fd < 0) {
        return -1;
    }

    if (setsockopt(server->socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        close(server->socket_fd);
        server->socket_fd = -1;
        return -1;
    }

    if (bind(server->socket_fd, (struct sockaddr *)&server->addr, sizeof(server->addr)) < 0) {
        close(server->socket_fd);
        server->socket_fd = -1;
        return -1;
    }

    if (listen(server->socket_fd, MAX_USERS) < 0) {
        close(server->socket_fd);
        server->socket_fd = -1;
        return -1;
    }

    signal(SIGPIPE, SIG_IGN);

    if (server_add_fd(server, server->socket_fd) < 0) {
        return -1;
    }

    if (server->stdin_message_callback != NULL && server_add_fd(server, STDIN_FILENO) == 0) {
        server->stdin_enabled = true;
    }

    return 0;
}

int server_run(struct Server *server) {
    fd_set read_fds = server->master_set;
    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 100000
    };

    int activity = select(server->max_fd + 1, &read_fds, NULL, NULL, &timeout);
    if (activity < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }
    if (activity == 0) {
        return 0;
    }

    for (int fd = 0; fd <= server->max_fd; fd++) {
        if (!FD_ISSET(fd, &read_fds)) {
            continue;
        }

        if (fd == server->socket_fd) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int new_fd = accept(server->socket_fd, (struct sockaddr *)&client_addr, &addr_len);

            if (new_fd >= 0 && server_add_fd(server, new_fd) < 0) {
                close(new_fd);
            }
            continue;
        }

        if (server->stdin_enabled && fd == STDIN_FILENO) {
            int result = server->stdin_message_callback(NULL);
            if (result == CALLBACK_REMOVE_STDIN) {
                server_remove_stdin(server);
            } else if (result < 0) {
                return -1;
            }
            continue;
        }

        if (server->client_message_callback != NULL) {
            int arg = fd;
            if (server->client_message_callback(&arg) < 0 && FD_ISSET(fd, &server->master_set)) {
                server_remove_client(server, fd);
            }
        }
    }

    return 0;
}

void server_shutdown(struct Server *server) {
    if (server == NULL) {
        return;
    }

    for (int fd = 0; fd <= server->max_fd; fd++) {
        if (FD_ISSET(fd, &server->master_set) && fd != STDIN_FILENO) {
            close(fd);
        }
    }

    free(server);
}
