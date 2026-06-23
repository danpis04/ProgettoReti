#include "include/common.h"
#include "include/server.h"
#include "include/protocol.h"
#include "include/database.h"
#include "include/lavagna_utils.h"

static volatile sig_atomic_t active = 1;
static struct Server *lavagna_server = NULL;

static void signal_handler(int signum) {
    (void)signum;
    active = 0;
}

static int read_card_id_payload(const struct Message *msg, int *card_id) {
    size_t offset = 0;

    if (msg->payload == NULL || msg->payload_length != sizeof(uint32_t)) {
        return -1;
    }

    *card_id = (int)read_u32((const char *)msg->payload, &offset);
    return 0;
}

static int handle_client_message(int socket_fd, const struct Message *msg) {
    in_port_t user_port = database_get_port_from_socket(socket_fd);

    switch (msg->type) {
        case MSG_HELLO: {
            size_t offset = 0;
            if (msg->payload == NULL || msg->payload_length != sizeof(uint32_t)) {
                return -1;
            }

            user_port = (in_port_t)read_u32((const char *)msg->payload, &offset);
            fprintf(stdout, "HELLO ricevuto dall'utente %u\n", (unsigned)user_port);

            if (database_add_user(socket_fd, user_port) < 0) {
                fprintf(stderr, "Registrazione fallita per la porta %u\n", (unsigned)user_port);
                return -1;
            }

            database_print_users();
            (void)send_available_card(lavagna_server);
            break;
        }

        case MSG_CREATE_CARD: {
            int created_id;
            fprintf(stdout, "CREATE_CARD ricevuto dall'utente %u\n", (unsigned)user_port);

            if (msg->payload_length == 0 || msg->payload == NULL
                    || ((const char *)msg->payload)[msg->payload_length - 1] != '\0') {
                fprintf(stderr, "CREATE_CARD richiede argomenti\n");
                break;
            }

            created_id = create_card_from_payload((const char *)msg->payload);
            if (created_id < 0) {
                fprintf(stderr, "Creazione card non valida\n");
                break;
            }

            fprintf(stdout, "Card %d creata\n", created_id);
            database_print_cards();
            (void)send_available_card(lavagna_server);
            break;
        }

        case MSG_SHOW_LAVAGNA:
            database_print_cards();
            break;

        case MSG_SEND_USER_LIST:
            if (send_user_list(socket_fd, user_port) < 0) {
                fprintf(stderr, "Invio lista utenti fallito verso %u\n", (unsigned)user_port);
            }
            break;

        case MSG_ACK_CARD: {
            int card_id;
            if (read_card_id_payload(msg, &card_id) < 0) {
                return -1;
            }

            fprintf(stdout, "ACK_CARD ricevuto da %u per la card %d\n", (unsigned)user_port, card_id);
            if (database_get_offered_card() != card_id
                    || database_card_doing(user_port, card_id) < 0) {
                fprintf(stderr, "ACK_CARD non valido da %u per la card %d\n",
                        (unsigned)user_port, card_id);
                break;
            }

            database_clear_offered_card();
            database_print_cards();
            break;
        }

        case MSG_CARD_DONE: {
            int card_id;
            if (read_card_id_payload(msg, &card_id) < 0) {
                return -1;
            }

            fprintf(stdout, "CARD_DONE ricevuto da %u per la card %d\n", (unsigned)user_port, card_id);
            if (database_card_done(user_port, card_id) < 0) {
                fprintf(stderr, "CARD_DONE non valido da %u per la card %d\n",
                        (unsigned)user_port, card_id);
                break;
            }

            database_print_cards();
            (void)send_available_card(lavagna_server);
            break;
        }

        case MSG_QUIT:
            fprintf(stdout, "QUIT ricevuto dall'utente %u\n", (unsigned)user_port);
            disconnect_user(lavagna_server, user_port);
            break;

        case MSG_PONG_LAVAGNA:
            fprintf(stdout, "PONG_LAVAGNA ricevuto dall'utente %u\n", (unsigned)user_port);
            (void)database_card_reset_timestamp(user_port);
            (void)database_user_clear_ping(user_port);
            break;

        default:
            fprintf(stderr, "Messaggio inatteso da socket %d: %s\n",
                    socket_fd, message_type_to_string(msg->type));
            break;
    }

    return 0;
}

static int handle_client(void *args) {
    int socket_fd = *(int *)args;
    char buffer[MAX_PAYLOAD_SIZE];
    struct Message msg = {
        .type = MSG_ERR,
        .payload_length = sizeof(buffer),
        .payload = buffer
    };

    ssize_t bytes = receive_message(socket_fd, &msg);
    if (bytes <= 0) {
        in_port_t port = database_get_port_from_socket(socket_fd);
        if (port != 0) {
            fprintf(stdout, "Connessione chiusa dall'utente %u\n", (unsigned)port);
            disconnect_user(lavagna_server, port);
            return 0;
        }
        return -1;
    }

    if (database_get_port_from_socket(socket_fd) == 0 && msg.type != MSG_HELLO) {
        fprintf(stderr, "Messaggio ricevuto da utente non registrato\n");
        return -1;
    }

    return handle_client_message(socket_fd, &msg);
}

static int handle_stdin_message(const struct Message *msg) {
    switch (msg->type) {
        case MSG_SHOW_LAVAGNA:
            database_print_cards();
            break;

        case MSG_SHOW_UTENTI:
            database_print_users();
            break;

        case MSG_CREATE_CARD: {
            int created_id;
            if (msg->payload_length == 0) {
                fprintf(stderr, "Uso: CREATE_CARD <id> <TODO|DOING|DONE> <testo>\n");
                break;
            }
            created_id = create_card_from_payload((const char *)msg->payload);
            if (created_id < 0) {
                fprintf(stderr, "Uso: CREATE_CARD <id> <TODO|DOING|DONE> <testo>\n");
                break;
            }
            fprintf(stdout, "Card %d creata\n", created_id);
            database_print_cards();
            (void)send_available_card(lavagna_server);
            break;
        }

        case MSG_MOVE_CARD:
            if (msg->payload_length == 0) {
                fprintf(stderr, "Uso: MOVE_CARD <id> <TODO|DOING|DONE> [porta]\n");
                break;
            }
            if (move_card_from_payload((const char *)msg->payload) < 0) {
                fprintf(stderr, "Uso: MOVE_CARD <id> <TODO|DOING|DONE> [porta]\n");
                break;
            }
            database_clear_offered_card();
            database_print_cards();
            (void)send_available_card(lavagna_server);
            break;

        case MSG_SEND_USER_LIST: {
            char *endptr;
            long port;
            int socket_fd;
            if (msg->payload_length == 0) {
                fprintf(stderr, "Uso: SEND_USER_LIST <porta>\n");
                break;
            }
            port = strtol((const char *)msg->payload, &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr, "Uso: SEND_USER_LIST <porta>\n");
                break;
            }
            socket_fd = database_get_socket_from_port((in_port_t)port);
            if (socket_fd < 0 || send_user_list(socket_fd, (in_port_t)port) < 0) {
                fprintf(stderr, "Utente %ld non disponibile\n", port);
            }
            break;
        }

        case MSG_PING_USER: {
            char *endptr;
            long port;
            int socket_fd;
            if (msg->payload_length == 0) {
                fprintf(stderr, "Uso: PING_USER <porta>\n");
                break;
            }
            port = strtol((const char *)msg->payload, &endptr, 10);
            if (*endptr != '\0') {
                fprintf(stderr, "Uso: PING_USER <porta>\n");
                break;
            }
            socket_fd = database_get_socket_from_port((in_port_t)port);
            if (socket_fd < 0 || send_user_ping(socket_fd) < 0) {
                fprintf(stderr, "PING_USER fallito verso %ld\n", port);
            }
            break;
        }

        case MSG_QUIT:
            active = 0;
            break;

        default:
            fprintf(stderr, "Comando non valido per la lavagna\n");
            break;
    }

    return 0;
}

static int handle_stdin(void *args) {
    (void)args;
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

    return handle_stdin_message(&msg);
}

int main(void) {
    struct ServerConfig config = {
        .port = SERVER_PORT,
        .client_message_callback = handle_client,
        .stdin_message_callback = handle_stdin
    };

    database_init();
    lavagna_server = server_create(config);
    if (lavagna_server == NULL) {
        database_cleanup();
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (server_init(lavagna_server) < 0) {
        perror("server_init");
        server_shutdown(lavagna_server);
        database_cleanup();
        return 1;
    }

    fprintf(stdout, "Lavagna avviata sulla porta %d\n", SERVER_PORT);
    database_print_cards();

    while (active && server_run(lavagna_server) == 0) {
        if (time(NULL) % TIMEOUT_CHECK_PERIOD == 0) {
            check_timeout(lavagna_server);
        }
    }

    server_shutdown(lavagna_server);
    database_cleanup();
    return 0;
}
