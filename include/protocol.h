#ifndef PROTOCOL_H
#define PROTOCOL_H

#include "common.h"

#define CALLBACK_REMOVE_STDIN -2

enum MessageType {
    MSG_ERR,

    MSG_HELLO,
    MSG_QUIT,
    MSG_CREATE_CARD,
    MSG_MOVE_CARD,
    MSG_SHOW_LAVAGNA,
    MSG_SHOW_UTENTI,
    MSG_SEND_USER_LIST,
    MSG_PING_USER,
    MSG_PONG_LAVAGNA,

    MSG_AVAILABLE_CARD,
    MSG_CHOOSE_USER,
    MSG_ACK_CARD,
    MSG_CARD_DONE,

    NUM_MSG_TYPES
};

struct Message {
    uint32_t type;
    uint32_t payload_length;
    void *payload;
};

ssize_t send_message(int socket_fd, const struct Message *msg);
ssize_t receive_message(int socket_fd, struct Message *msg);
ssize_t get_command_line_input(struct Message *msg);
const char *message_type_to_string(uint32_t type);

void write_u32(char *buffer, size_t *offset, uint32_t value);
uint32_t read_u32(const char *buffer, size_t *offset);

#endif
