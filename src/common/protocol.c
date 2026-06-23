#include "../../include/protocol.h"

static const char *MESSAGE_TO_STRING[NUM_MSG_TYPES] = {
    [MSG_HELLO] = "HELLO",
    [MSG_QUIT] = "QUIT",
    [MSG_CREATE_CARD] = "CREATE_CARD",
    [MSG_MOVE_CARD] = "MOVE_CARD",
    [MSG_SHOW_LAVAGNA] = "SHOW_LAVAGNA",
    [MSG_SHOW_UTENTI] = "SHOW_UTENTI",
    [MSG_SEND_USER_LIST] = "SEND_USER_LIST",
    [MSG_PING_USER] = "PING_USER",
    [MSG_PONG_LAVAGNA] = "PONG_LAVAGNA",
    [MSG_AVAILABLE_CARD] = "AVAILABLE_CARD",
    [MSG_CHOOSE_USER] = "CHOOSE_USER",
    [MSG_ACK_CARD] = "ACK_CARD",
    [MSG_CARD_DONE] = "CARD_DONE"
};

const char *message_type_to_string(uint32_t type) {
    if (type >= NUM_MSG_TYPES || MESSAGE_TO_STRING[type] == NULL) {
        return "UNKNOWN";
    }
    return MESSAGE_TO_STRING[type];
}

void write_u32(char *buffer, size_t *offset, uint32_t value) {
    uint32_t network_value = htonl(value);
    memcpy(buffer + *offset, &network_value, sizeof(network_value));
    *offset += sizeof(network_value);
}

uint32_t read_u32(const char *buffer, size_t *offset) {
    uint32_t network_value;
    memcpy(&network_value, buffer + *offset, sizeof(network_value));
    *offset += sizeof(network_value);
    return ntohl(network_value);
}

static ssize_t send_all(int socket_fd, const void *buffer, size_t length) {
    size_t sent = 0;
    const char *cursor = (const char *)buffer;

    while (sent < length) {
        ssize_t chunk = send(socket_fd, cursor + sent, length - sent, 0);
        if (chunk < 0 && errno == EINTR) {
            continue;
        }
        if (chunk <= 0) {
            return -1;
        }
        sent += (size_t)chunk;
    }

    return (ssize_t)sent;
}

static ssize_t recv_all(int socket_fd, void *buffer, size_t length) {
    size_t received = 0;
    char *cursor = (char *)buffer;

    while (received < length) {
        ssize_t chunk = recv(socket_fd, cursor + received, length - received, 0);
        if (chunk < 0 && errno == EINTR) {
            continue;
        }
        if (chunk <= 0) {
            return chunk;
        }
        received += (size_t)chunk;
    }

    return (ssize_t)received;
}

ssize_t send_message(int socket_fd, const struct Message *msg) {
    uint32_t header[2];
    size_t payload_length = msg->payload_length;

    if (payload_length > 0 && msg->payload == NULL) {
        return -1;
    }

    header[0] = htonl(msg->type);
    header[1] = htonl(msg->payload_length);

    if (send_all(socket_fd, header, sizeof(header)) < 0) {
        return -1;
    }

    if (payload_length > 0 && send_all(socket_fd, msg->payload, payload_length) < 0) {
        return -1;
    }

    return (ssize_t)(sizeof(header) + payload_length);
}

ssize_t receive_message(int socket_fd, struct Message *msg) {
    uint32_t header[2];
    uint32_t capacity = msg->payload_length;

    ssize_t header_bytes = recv_all(socket_fd, header, sizeof(header));
    if (header_bytes <= 0) {
        return header_bytes;
    }

    msg->type = ntohl(header[0]);
    msg->payload_length = ntohl(header[1]);

    if (msg->payload_length > capacity || msg->payload_length > MAX_PAYLOAD_SIZE) {
        return -1;
    }

    if (msg->payload_length == 0) {
        return header_bytes;
    }

    if (msg->payload == NULL) {
        return -1;
    }

    ssize_t payload_bytes = recv_all(socket_fd, msg->payload, msg->payload_length);
    if (payload_bytes <= 0) {
        return payload_bytes;
    }

    return header_bytes + payload_bytes;
}

static ssize_t copy_payload(struct Message *msg, const char *payload) {
    size_t length = strlen(payload);

    if (length + 1 > MAX_PAYLOAD_SIZE || msg->payload == NULL) {
        return -1;
    }

    memcpy(msg->payload, payload, length + 1);
    msg->payload_length = (uint32_t)(length + 1);
    return (ssize_t)length;
}

static int command_matches(const char *input, const char *command, size_t command_length) {
    return strncmp(input, command, command_length) == 0
        && (input[command_length] == '\0' || input[command_length] == ' ');
}

ssize_t get_command_line_input(struct Message *msg) {
    char buffer[MAX_PAYLOAD_SIZE];
    ssize_t input_length = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);

    if (input_length < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }
    if (input_length == 0) {
        return CALLBACK_REMOVE_STDIN;
    }

    buffer[input_length] = '\0';
    if (input_length > 0 && buffer[input_length - 1] == '\n') {
        buffer[input_length - 1] = '\0';
        input_length--;
    }

    msg->type = MSG_ERR;
    msg->payload_length = 0;

    if (input_length == 0) {
        return 0;
    }

    for (uint32_t type = 1; type < NUM_MSG_TYPES; type++) {
        const char *command = MESSAGE_TO_STRING[type];
        size_t command_length;

        if (command == NULL) {
            continue;
        }

        command_length = strlen(command);
        if (command_matches(buffer, command, command_length)) {
            const char *payload = buffer + command_length;
            while (*payload == ' ') {
                payload++;
            }

            msg->type = type;
            if (*payload != '\0') {
                return copy_payload(msg, payload);
            }
            return input_length;
        }
    }

    fprintf(stderr, "Comando non riconosciuto: %s\n", buffer);
    return -1;
}
