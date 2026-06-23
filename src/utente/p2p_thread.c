#include "../../include/thread.h"
#include "../../include/server.h"
#include "../../include/protocol.h"
#include "../../include/utente_state.h"

static int handle_peer_message(const struct Message *msg) {
    if (msg->type == MSG_CHOOSE_USER) {
        size_t offset = 0;
        int card_id;
        in_port_t sender_port;
        int cost;

        if (msg->payload == NULL || msg->payload_length != sizeof(uint32_t) * 3) {
            return -1;
        }

        card_id = (int)read_u32((const char *)msg->payload, &offset);
        sender_port = (in_port_t)read_u32((const char *)msg->payload, &offset);
        cost = (int)read_u32((const char *)msg->payload, &offset);
        utente_record_choice(card_id, sender_port, cost);
    } else {
        fprintf(stderr, "Messaggio P2P inatteso: %s\n", message_type_to_string(msg->type));
    }

    return -1;
}

static int handle_peer(void *args) {
    int socket_fd = *(int *)args;
    char buffer[MAX_PAYLOAD_SIZE];
    struct Message msg = {
        .type = MSG_ERR,
        .payload_length = sizeof(buffer),
        .payload = buffer
    };

    if (receive_message(socket_fd, &msg) <= 0) {
        return -1;
    }

    return handle_peer_message(&msg);
}

void *p2p_server_function(void *arg) {
    (void)arg;

    struct ServerConfig config = {
        .port = utente_get_port(),
        .client_message_callback = handle_peer,
        .stdin_message_callback = NULL
    };
    struct Server *server = server_create(config);

    if (server == NULL || server_init(server) < 0) {
        server_shutdown(server);
        utente_update_state(STATE_SHUTTING_DOWN);
        return NULL;
    }

    utente_update_state(STATE_CONNECTING);
    while (utente_get_state() != STATE_DISCONNECTING
            && utente_get_state() != STATE_SHUTTING_DOWN
            && server_run(server) == 0) {
    }

    server_shutdown(server);
    utente_update_state(STATE_SHUTTING_DOWN);
    return NULL;
}
