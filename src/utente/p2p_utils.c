#include "../../include/p2p_utils.h"
#include "../../include/protocol.h"
#include "../../include/utente_state.h"

// Apre una connessione breve verso un peer e invia la proposta di costo.
static int send_choose_to_peer(in_port_t peer_port, int card_id, int cost) {
    int peer_socket = socket(AF_INET, SOCK_STREAM, 0);
    char payload[sizeof(uint32_t) * 3];
    size_t offset = 0;

    if (peer_socket < 0) {
        return -1;
    }

    struct sockaddr_in peer_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(peer_port),
        .sin_addr.s_addr = inet_addr(LOCALHOST)
    };

    if (connect(peer_socket, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
        close(peer_socket);
        return -1;
    }

    write_u32(payload, &offset, (uint32_t)card_id);
    write_u32(payload, &offset, (uint32_t)utente_get_port());
    write_u32(payload, &offset, (uint32_t)cost);

    // Payload: id card, porta mittente, costo proposto.
    struct Message msg = {
        .type = MSG_CHOOSE_USER,
        .payload_length = (uint32_t)offset,
        .payload = payload
    };

    if (send_message(peer_socket, &msg) < 0) {
        close(peer_socket);
        return -1;
    }

    close(peer_socket);
    return 0;
}

int p2p_broadcast_choose(int card_id, int cost, const in_port_t *peers, int peer_count) {
    int sent = 0;

    // Ogni peer riceve un messaggio indipendente.
    for (int i = 0; i < peer_count; i++) {
        if (send_choose_to_peer(peers[i], card_id, cost) == 0) {
            sent++;
        } else {
            fprintf(stderr, "CHOOSE_USER verso %u non riuscito\n", (unsigned)peers[i]);
        }
    }

    return sent;
}
