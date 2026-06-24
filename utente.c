#include "include/common.h"
#include "include/client.h"
#include "include/protocol.h"
#include "include/thread.h"
#include "include/utente_state.h"
#include "include/utente_utils.h"
#include "include/p2p_utils.h"

static volatile sig_atomic_t running = 1;

static void signal_handler(int signum) {
    (void)signum;
    running = 0;
}

static void pause_milliseconds(long milliseconds) {
    struct timeval delay;
    delay.tv_sec = milliseconds / 1000;
    delay.tv_usec = (milliseconds % 1000) * 1000;
    (void)select(0, NULL, NULL, NULL, &delay);
}

static int parse_available_card(const struct Message *msg, int *card_id, int *total_users,
                                in_port_t *peers, int *peer_count, char *text,
                                size_t text_size) {
    size_t offset = 0;
    size_t peer_bytes;
    size_t text_length;

    if (msg->payload == NULL || msg->payload_length < sizeof(uint32_t) * 2) {
        return -1;
    }

    *card_id = (int)read_u32((const char *)msg->payload, &offset);
    *total_users = (int)read_u32((const char *)msg->payload, &offset);
    if (*total_users < 2 || *total_users > MAX_USERS) {
        return -1;
    }
    *peer_count = *total_users - 1;

    peer_bytes = (size_t)(*peer_count) * sizeof(uint32_t);
    if (msg->payload_length < offset + peer_bytes + 1) {
        return -1;
    }

    for (int i = 0; i < *peer_count; i++) {
        peers[i] = (in_port_t)read_u32((const char *)msg->payload, &offset);
    }

    text_length = msg->payload_length - offset;
    if (text_length == 0 || ((const char *)msg->payload)[msg->payload_length - 1] != '\0') {
        return -1;
    }

    snprintf(text, text_size, "%s", (const char *)msg->payload + offset);
    return 0;
}

static int parse_optional_card_id(const struct Message *msg, int *card_id) {
    char *endptr;
    long value;

    *card_id = -1;
    if (msg->payload_length == 0) {
        return 0;
    }

    value = strtol((const char *)msg->payload, &endptr, 10);
    if (*endptr != '\0' || value <= 0) {
        return -1;
    }

    *card_id = (int)value;
    return 0;
}

static int handle_stdin_message(int server_fd, const struct Message *msg) {
    switch (msg->type) {
        case MSG_HELLO:
            if (send_server_hello(server_fd) < 0) {
                return -1;
            }
            break;

        case MSG_CREATE_CARD:
        case MSG_SHOW_LAVAGNA:
            if (send_message(server_fd, msg) < 0) {
                return -1;
            }
            break;

        case MSG_CHOOSE_USER: {
            char *endptr;
            long value;
            int cost;
            int card_id = utente_get_card_id();
            in_port_t peers[MAX_USERS];
            int peer_count;

            if (card_id <= 0) {
                fprintf(stderr, "CHOOSE_USER non disponibile per l'utente corrente\n");
                break;
            }

            if (msg->payload_length == 0) {
                cost = utente_get_own_cost();
            } else {
                value = strtol((const char *)msg->payload, &endptr, 10);
                if (*endptr != '\0') {
                    fprintf(stderr, "Uso: CHOOSE_USER [costo]\n");
                    break;
                }
                cost = (int)value;
            }

            peer_count = utente_copy_peers(peers, MAX_USERS);
            if (peer_count <= 0) {
                fprintf(stderr, "CHOOSE_USER non disponibile per l'utente corrente\n");
                break;
            }

            fprintf(stdout, "Invio CHOOSE_USER per card %d con costo %d\n", card_id, cost);
            fflush(stdout);
            (void)p2p_broadcast_choose(card_id, cost, peers, peer_count);
            break;
        }

        case MSG_ACK_CARD: {
            int requested_card_id;
            int card_id;

            if (parse_optional_card_id(msg, &requested_card_id) < 0) {
                fprintf(stderr, "Uso: ACK_CARD [id]\n");
                break;
            }
            if (!utente_take_manual_ack_action(requested_card_id, &card_id)) {
                fprintf(stderr, "ACK_CARD non disponibile per l'utente corrente\n");
                break;
            }

            fprintf(stdout, "Invio ACK_CARD per card %d\n", card_id);
            fflush(stdout);
            if (send_server_ack_card(server_fd, card_id) < 0) {
                return -1;
            }
            if (utente_start_worker(worker_thread_function) < 0) {
                return -1;
            }
            break;
        }

        case MSG_CARD_DONE: {
            int requested_card_id;
            int card_id;

            if (parse_optional_card_id(msg, &requested_card_id) < 0) {
                fprintf(stderr, "Uso: CARD_DONE [id]\n");
                break;
            }

            if (!utente_take_manual_done_action(requested_card_id, &card_id)) {
                fprintf(stderr, "CARD_DONE non disponibile per l'utente corrente\n");
                break;
            }
            fprintf(stdout, "Invio CARD_DONE per card %d\n", card_id);
            fflush(stdout);
            if (send_server_card_done(server_fd, card_id) < 0) {
                return -1;
            }
            break;
        }

        case MSG_PONG_LAVAGNA:
            if (msg->payload_length != 0) {
                fprintf(stderr, "Uso: PONG_LAVAGNA\n");
                break;
            }
            if (send_server_pong(server_fd) < 0) {
                return -1;
            }
            break;

        case MSG_QUIT:
            (void)send_message(server_fd, msg);
            running = 0;
            break;

        default:
            fprintf(stderr, "Comando non valido per utente\n");
            break;
    }

    return 0;
}

static int handle_stdin(void *args) {
    int server_fd = *(int *)args;
    char buffer[MAX_PAYLOAD_SIZE];
    struct Message msg = {
        .type = MSG_ERR,
        .payload_length = sizeof(buffer),
        .payload = buffer
    };

    ssize_t result = get_command_line_input(&msg);
    if (result == CALLBACK_REMOVE_STDIN) {
        return CALLBACK_REMOVE_STDIN;
    }
    if (result <= 0) {
        return 0;
    }

    return handle_stdin_message(server_fd, &msg);
}

static int handle_available_card(const struct Message *msg) {
    int card_id;
    int total_users;
    int peer_count;
    in_port_t peers[MAX_USERS];
    char text[CARD_TEXT_SIZE];

    if (parse_available_card(msg, &card_id, &total_users, peers, &peer_count,
                             text, sizeof(text)) < 0) {
        return -1;
    }

    int n = rand();

    utente_start_election(card_id, text, total_users, peers, peer_count, n);
    pause_milliseconds(200);
    (void)p2p_broadcast_choose(card_id, n, peers, peer_count);

    return 0;
}

static int handle_server_message(int server_fd, const struct Message *msg) {
    switch (msg->type) {
        case MSG_AVAILABLE_CARD:
            return handle_available_card(msg);

        case MSG_SEND_USER_LIST: {
            size_t offset = 0;
            int total;
            int count;
            uint32_t raw_count;
            size_t expected_length;

            if (msg->payload == NULL || msg->payload_length < sizeof(uint32_t) * 2) {
                return -1;
            }

            total = (int)read_u32((const char *)msg->payload, &offset);
            raw_count = read_u32((const char *)msg->payload, &offset);
            if (raw_count > MAX_USERS) {
                return -1;
            }

            expected_length = sizeof(uint32_t) * 2 + (size_t)raw_count * sizeof(uint32_t);
            if (msg->payload_length != expected_length) {
                return -1;
            }

            count = (int)raw_count;
            fprintf(stdout, "Lista utenti ricevuta: totale %d, altri %d\n", total, count);
            for (int i = 0; i < count; i++) {
                in_port_t port = (in_port_t)read_u32((const char *)msg->payload, &offset);
                fprintf(stdout, "Peer: %u\n", (unsigned)port);
            }
            fflush(stdout);
            break;
        }

        case MSG_PING_USER:
            fprintf(stdout, "PING_USER ricevuto dalla lavagna\n");
            if (send_server_pong(server_fd) < 0) {
                return -1;
            }
            break;

        case MSG_QUIT:
            fprintf(stdout, "Chiusura richiesta dalla lavagna\n");
            return -1;

        default:
            fprintf(stderr, "Messaggio inatteso dalla lavagna: %s\n",
                    message_type_to_string(msg->type));
            break;
    }

    return 0;
}

static int handle_server(void *args) {
    int server_fd = *(int *)args;
    char buffer[MAX_PAYLOAD_SIZE];
    struct Message msg = {
        .type = MSG_ERR,
        .payload_length = sizeof(buffer),
        .payload = buffer
    };

    ssize_t bytes = receive_message(server_fd, &msg);
    if (bytes <= 0) {
        fprintf(stdout, "Connessione con lavagna chiusa\n");
        return -1;
    }

    return handle_server_message(server_fd, &msg);
}

static int run_pending_actions(int server_fd) {
    int card_id;

    if (utente_take_ack_action(&card_id)) {
        fprintf(stdout, "Invio ACK_CARD per card %d\n", card_id);
        if (send_server_ack_card(server_fd, card_id) < 0) {
            return -1;
        }

        utente_mark_working();
        if (utente_start_worker(worker_thread_function) < 0) {
            return -1;
        }
    }

    if (utente_take_done_action(&card_id)) {
        utente_wait_for_worker_shutdown();
        fprintf(stdout, "Invio CARD_DONE per card %d\n", card_id);
        if (send_server_card_done(server_fd, card_id) < 0) {
            return -1;
        }
    }

    return 0;
}

static int parse_port_argument(const char *text, in_port_t *port) {
    char *endptr;
    long value = strtol(text, &endptr, 10);

    if (*endptr != '\0' || value < MIN_USER_PORT || value > 65535) {
        return -1;
    }

    *port = (in_port_t)value;
    return 0;
}

static int start_p2p_on_port(in_port_t port) {
    utente_init(port);

    if (utente_start_p2p(p2p_server_function) < 0) {
        utente_cleanup();
        return -1;
    }

    while (utente_get_state() == STATE_STARTING_P2P) {
        pause_milliseconds(50);
    }

    if (utente_get_state() == STATE_CONNECTING) {
        return 0;
    }

    utente_wait_for_p2p_shutdown();
    utente_cleanup();
    return -1;
}

static in_port_t start_p2p_on_first_free_port(void) {
    for (int candidate = MIN_USER_PORT; candidate <= 65535; candidate++) {
        if (start_p2p_on_port((in_port_t)candidate) == 0) {
            return (in_port_t)candidate;
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    in_port_t port;
    struct Client *client;
    struct ClientConfig config = {
        .server_ip = 0,
        .server_port = SERVER_PORT,
        .server_message_callback = handle_server,
        .stdin_message_callback = handle_stdin
    };

    if (argc > 2) {
        fprintf(stderr, "Uso: %s [porta]\n", argv[0]);
        return 1;
    }

    if (argc == 2 && parse_port_argument(argv[1], &port) < 0) {
        fprintf(stderr, "Porta utente non valida\n");
        return 1;
    }

    if (inet_pton(AF_INET, LOCALHOST, &config.server_ip) != 1) {
        fprintf(stderr, "Indirizzo lavagna non valido\n");
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (argc == 2) {
        if (start_p2p_on_port(port) < 0) {
            fprintf(stderr, "Porta utente %u non disponibile\n", (unsigned)port);
            return 1;
        }
    } else {
        port = start_p2p_on_first_free_port();
    }
    if (port == 0) {
        fprintf(stderr, "Nessuna porta utente disponibile\n");
        return 1;
    }

    client = client_create(config);
    if (client == NULL) {
        utente_update_state(STATE_DISCONNECTING);
        utente_wait_for_p2p_shutdown();
        utente_cleanup();
        return 1;
    }

    if (client_connect_to_server(client) < 0) {
        fprintf(stderr, "Connessione alla lavagna fallita\n");
        client_destroy(client);
        utente_update_state(STATE_DISCONNECTING);
        utente_wait_for_p2p_shutdown();
        utente_cleanup();
        return 1;
    }

    fprintf(stdout, "Utente avviato sulla porta %u\n", (unsigned)port);
    fflush(stdout);
    if (send_server_hello(client->server_socket) < 0) {
        fprintf(stderr, "Invio HELLO fallito\n");
        client_destroy(client);
        utente_update_state(STATE_DISCONNECTING);
        utente_wait_for_p2p_shutdown();
        utente_cleanup();
        return 1;
    }
    utente_update_state(STATE_IDLE);

    while (running && utente_get_state() != STATE_SHUTTING_DOWN) {
        if (client_listen(client) < 0) {
            running = 0;
            break;
        }
        if (run_pending_actions(client->server_socket) < 0) {
            running = 0;
            break;
        }
    }

    if (client->server_socket >= 0) {
        struct Message quit_msg = {
            .type = MSG_QUIT,
            .payload_length = 0,
            .payload = NULL
        };
        (void)send_message(client->server_socket, &quit_msg);
    }

    utente_wait_for_worker_shutdown();
    client_destroy(client);
    utente_update_state(STATE_DISCONNECTING);
    utente_wait_for_p2p_shutdown();
    utente_cleanup();
    return 0;
}
