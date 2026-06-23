#include "../../include/lavagna_utils.h"
#include "../../include/database.h"
#include "../../include/protocol.h"

static enum CardStatus parse_card_status(const char *text) {
    if (strcmp(text, "TODO") == 0 || strcmp(text, "TO_DO") == 0) {
        return CARD_STATUS_TODO;
    }
    if (strcmp(text, "DOING") == 0) {
        return CARD_STATUS_DOING;
    }
    if (strcmp(text, "DONE") == 0) {
        return CARD_STATUS_DONE;
    }
    return CARD_STATUS_TODO;
}

int send_user_list(int socket_fd, in_port_t excluded_port) {
    in_port_t *ports = NULL;
    int count = database_get_users_list_except(excluded_port, &ports);
    char buffer[MAX_PAYLOAD_SIZE];
    size_t offset = 0;

    if (count < 0) {
        return -1;
    }

    write_u32(buffer, &offset, (uint32_t)database_get_num_users());
    write_u32(buffer, &offset, (uint32_t)count);
    for (int i = 0; i < count; i++) {
        write_u32(buffer, &offset, (uint32_t)ports[i]);
    }

    struct Message msg = {
        .type = MSG_SEND_USER_LIST,
        .payload_length = (uint32_t)offset,
        .payload = buffer
    };

    free(ports);
    return send_message(socket_fd, &msg) < 0 ? -1 : 0;
}

int send_user_ping(int socket_fd) {
    in_port_t user_port = database_get_port_from_socket(socket_fd);
    struct Message msg = {
        .type = MSG_PING_USER,
        .payload_length = 0,
        .payload = NULL
    };

    if (user_port == 0 || database_user_get_status(user_port) != USER_STATUS_ACTIVE) {
        return -1;
    }
    if (send_message(socket_fd, &msg) < 0) {
        return -1;
    }

    (void)database_user_set_ping(user_port);
    fprintf(stdout, "PING_USER inviato all'utente %u\n", (unsigned)user_port);
    fflush(stdout);
    return 0;
}

int send_user_quit(int socket_fd) {
    struct Message msg = {
        .type = MSG_QUIT,
        .payload_length = 0,
        .payload = NULL
    };

    return send_message(socket_fd, &msg) < 0 ? -1 : 0;
}

static int fill_available_payload(char *buffer, int card_id, in_port_t recipient_port) {
    in_port_t *peers = NULL;
    int peer_count = database_get_users_list_except(recipient_port, &peers);
    char text[CARD_TEXT_SIZE];
    size_t offset = 0;

    if (peer_count < 0 || database_card_get_text(card_id, text, sizeof(text)) < 0) {
        free(peers);
        return -1;
    }

    write_u32(buffer, &offset, (uint32_t)card_id);
    write_u32(buffer, &offset, (uint32_t)database_get_num_users());
    write_u32(buffer, &offset, (uint32_t)peer_count);

    for (int i = 0; i < peer_count; i++) {
        write_u32(buffer, &offset, (uint32_t)peers[i]);
    }

    if (offset + strlen(text) + 1 > MAX_PAYLOAD_SIZE) {
        free(peers);
        return -1;
    }

    memcpy(buffer + offset, text, strlen(text) + 1);
    offset += strlen(text) + 1;
    free(peers);

    return (int)offset;
}

int send_available_card(struct Server *server) {
    in_port_t *users = NULL;
    int user_count;
    int card_id;

    if (database_get_offered_card() != -1 || database_has_doing_cards()
            || database_get_num_users() <= 1) {
        return 0;
    }

    card_id = database_get_next_todo_card();
    if (card_id == -1) {
        return 0;
    }

    user_count = database_get_users_list(&users);
    if (user_count <= 1) {
        free(users);
        return 0;
    }

    database_set_offered_card(card_id);

    for (int i = 0; i < user_count; i++) {
        char payload[MAX_PAYLOAD_SIZE];
        int payload_length = fill_available_payload(payload, card_id, users[i]);
        int socket_fd = database_get_socket_from_port(users[i]);
        struct Message msg = {
            .type = MSG_AVAILABLE_CARD,
            .payload_length = (uint32_t)payload_length,
            .payload = payload
        };

        if (payload_length < 0 || socket_fd < 0 || send_message(socket_fd, &msg) < 0) {
            fprintf(stderr, "Impossibile inviare AVAILABLE_CARD all'utente %u\n", (unsigned)users[i]);
            disconnect_user(server, users[i]);
        }
    }

    fprintf(stdout, "AVAILABLE_CARD inviato per la card %d a %d utenti\n", card_id, user_count);
    fflush(stdout);
    free(users);
    return 0;
}

int create_card_from_payload(const char *payload) {
    char local[MAX_PAYLOAD_SIZE];
    char *id_text;
    char *column_text;
    char *body;
    char *endptr;
    long id;

    if (payload == NULL || payload[0] == '\0') {
        return -1;
    }

    snprintf(local, sizeof(local), "%s", payload);
    id_text = strtok(local, " ");
    if (id_text == NULL) {
        return -1;
    }

    id = strtol(id_text, &endptr, 10);
    if (*endptr != '\0') {
        return database_create_card(0, CARD_STATUS_TODO, payload);
    }

    column_text = strtok(NULL, " ");
    body = strtok(NULL, "");
    if (column_text == NULL || body == NULL || body[0] == '\0') {
        return -1;
    }

    return database_create_card((int)id, parse_card_status(column_text), body);
}

int move_card_from_payload(const char *payload) {
    char local[MAX_PAYLOAD_SIZE];
    char *id_text;
    char *status_text;
    char *port_text;
    char *endptr;
    long id;
    long port = 0;

    if (payload == NULL) {
        return -1;
    }

    snprintf(local, sizeof(local), "%s", payload);
    id_text = strtok(local, " ");
    status_text = strtok(NULL, " ");
    port_text = strtok(NULL, " ");
    if (id_text == NULL || status_text == NULL) {
        return -1;
    }

    id = strtol(id_text, &endptr, 10);
    if (*endptr != '\0') {
        return -1;
    }

    if (port_text != NULL) {
        port = strtol(port_text, &endptr, 10);
        if (*endptr != '\0') {
            return -1;
        }
    }

    return database_move_card((int)id, parse_card_status(status_text), (in_port_t)port);
}

void disconnect_user(struct Server *server, in_port_t user_port) {
    int socket_fd = database_get_socket_from_port(user_port);

    if (socket_fd >= 0) {
        (void)server_remove_client(server, socket_fd);
    }

    (void)database_card_todo(user_port);
    (void)database_remove_user(user_port);
    database_clear_offered_card();

    fprintf(stdout, "Utente %u rimosso dalla lavagna\n", (unsigned)user_port);
    database_print_users();
    database_print_cards();
    (void)send_available_card(server);
}

static void check_working_timeout(struct Server *server) {
    int *card_ids = NULL;
    int count = database_card_get_timed_out(&card_ids, WORKING_TIMEOUT_SECONDS);

    if (count <= 0) {
        free(card_ids);
        return;
    }

    for (int i = 0; i < count; i++) {
        in_port_t port = database_card_get_user(card_ids[i]);
        int socket_fd = database_get_socket_from_port(port);
        if (socket_fd >= 0) {
            (void)send_user_ping(socket_fd);
        } else {
            disconnect_user(server, port);
        }
    }

    free(card_ids);
}

static void check_ping_timeout(struct Server *server) {
    in_port_t *ports = NULL;
    int count = database_user_get_timed_out(&ports, PING_TIMEOUT_SECONDS);

    if (count <= 0) {
        free(ports);
        return;
    }

    for (int i = 0; i < count; i++) {
        int socket_fd = database_get_socket_from_port(ports[i]);
        if (socket_fd >= 0) {
            (void)send_user_quit(socket_fd);
        }
        disconnect_user(server, ports[i]);
    }

    free(ports);
}

void check_timeout(struct Server *server) {
    check_working_timeout(server);
    check_ping_timeout(server);
}
