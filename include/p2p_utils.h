#ifndef P2P_UTILS_H
#define P2P_UTILS_H

#include "common.h"

// Invia la proposta CHOOSE_USER a tutti i peer noti.
int p2p_broadcast_choose(int card_id, int cost, const in_port_t *peers, int peer_count);

#endif
