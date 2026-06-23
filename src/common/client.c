#include "../../include/client.h"
#include "../../include/protocol.h"

struct Client *client_create(struct ClientConfig config) {
    struct Client *client = malloc(sizeof(*client));
    if (client == NULL) {
        return NULL;
    }

    client->server_socket = -1;
    client->server_addr.sin_family = AF_INET;
    client->server_addr.sin_port = htons(config.server_port);
    client->server_addr.sin_addr.s_addr = (uint32_t)config.server_ip;
    FD_ZERO(&client->master_set);
    client->max_fd = -1;
    client->stdin_enabled = false;
    client->server_message_callback = config.server_message_callback;
    client->stdin_message_callback = config.stdin_message_callback;

    if (client->stdin_message_callback != NULL) {
        FD_SET(STDIN_FILENO, &client->master_set);
        client->stdin_enabled = true;
        client->max_fd = STDIN_FILENO;
    }

    return client;
}

int client_connect_to_server(struct Client *client) {
    client->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client->server_socket < 0) {
        return -1;
    }

    if (connect(client->server_socket, (struct sockaddr *)&client->server_addr,
                sizeof(client->server_addr)) < 0) {
        close(client->server_socket);
        client->server_socket = -1;
        return -1;
    }

    FD_SET(client->server_socket, &client->master_set);
    if (client->server_socket > client->max_fd) {
        client->max_fd = client->server_socket;
    }

    return 0;
}

static void client_refresh_max_fd(struct Client *client) {
    while (client->max_fd >= 0 && !FD_ISSET(client->max_fd, &client->master_set)) {
        client->max_fd--;
    }
}

static void client_remove_stdin(struct Client *client) {
    FD_CLR(STDIN_FILENO, &client->master_set);
    client->stdin_enabled = false;
    if (client->max_fd == STDIN_FILENO) {
        client_refresh_max_fd(client);
    }
}

int client_listen(struct Client *client) {
    fd_set read_fds = client->master_set;
    struct timeval timeout = {
        .tv_sec = 0,
        .tv_usec = 100000
    };

    int activity = select(client->max_fd + 1, &read_fds, NULL, NULL, &timeout);
    if (activity < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }
    if (activity == 0) {
        return 0;
    }

    if (client->stdin_enabled && FD_ISSET(STDIN_FILENO, &read_fds)) {
        int result = client->stdin_message_callback(&client->server_socket);
        if (result == CALLBACK_REMOVE_STDIN) {
            client_remove_stdin(client);
        } else if (result < 0) {
            return -1;
        }
    }

    if (client->server_socket >= 0 && FD_ISSET(client->server_socket, &read_fds)) {
        if (client->server_message_callback(&client->server_socket) < 0) {
            return -1;
        }
    }

    return 0;
}

void client_destroy(struct Client *client) {
    if (client == NULL) {
        return;
    }

    if (client->server_socket >= 0) {
        close(client->server_socket);
        client->server_socket = -1;
    }

    free(client);
}
