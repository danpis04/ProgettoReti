#include "../../include/utente_utils.h"
#include "../../include/protocol.h"
#include "../../include/utente_state.h"

// Helper per i messaggi che hanno come unico payload l'id della card.
static int send_card_id_message(int server_fd, uint32_t type, int card_id) {
    char payload[sizeof(uint32_t)];
    size_t offset = 0;

    write_u32(payload, &offset, (uint32_t)card_id);

    struct Message msg = {
        .type = type,
        .payload_length = (uint32_t)offset,
        .payload = payload
    };

    return send_message(server_fd, &msg) < 0 ? -1 : 0;
}

int send_server_hello(int server_fd) {
    char payload[sizeof(uint32_t)];
    size_t offset = 0;

    write_u32(payload, &offset, (uint32_t)utente_get_port());

    struct Message msg = {
        .type = MSG_HELLO,
        .payload_length = (uint32_t)offset,
        .payload = payload
    };

    // La HELLO contiene la porta su cui l'utente accetta messaggi P2P.
    return send_message(server_fd, &msg) < 0 ? -1 : 0;
}

int send_server_ack_card(int server_fd, int card_id) {
    return send_card_id_message(server_fd, MSG_ACK_CARD, card_id);
}

int send_server_card_done(int server_fd, int card_id) {
    return send_card_id_message(server_fd, MSG_CARD_DONE, card_id);
}

int send_server_pong(int server_fd) {
    struct Message msg = {
        .type = MSG_PONG_LAVAGNA,
        .payload_length = 0,
        .payload = NULL
    };

    return send_message(server_fd, &msg) < 0 ? -1 : 0;
}
