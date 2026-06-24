#include "../../include/database.h"

static struct Database database;

static const char *USER_STATUS_TEXT[] = {
    [USER_STATUS_EMPTY] = "EMPTY",
    [USER_STATUS_IDLE] = "IDLE",
    [USER_STATUS_ACTIVE] = "ACTIVE",
    [USER_STATUS_PINGED] = "PINGED"
};

static const char *CARD_STATUS_TEXT[] = {
    [CARD_STATUS_TODO] = "To Do",
    [CARD_STATUS_DOING] = "Doing",
    [CARD_STATUS_DONE] = "Done"
};

static int find_user_index(in_port_t port) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (database.users[i].used && database.users[i].port == port) {
            return i;
        }
    }
    return -1;
}

static int find_user_index_by_socket(int socket_fd) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (database.users[i].used && database.users[i].socket_fd == socket_fd) {
            return i;
        }
    }
    return -1;
}

static int find_free_user_index(void) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (!database.users[i].used) {
            return i;
        }
    }
    return -1;
}

static int find_card_index(int card_id) {
    for (int i = 0; i < MAX_CARDS; i++) {
        if (database.cards[i].used && database.cards[i].id == card_id) {
            return i;
        }
    }
    return -1;
}

static int find_free_card_index(void) {
    for (int i = 0; i < MAX_CARDS; i++) {
        if (!database.cards[i].used) {
            return i;
        }
    }
    return -1;
}

static int next_card_id(void) {
    int next_id = 1;

    for (int i = 0; i < MAX_CARDS; i++) {
        if (database.cards[i].used && database.cards[i].id >= next_id) {
            next_id = database.cards[i].id + 1;
        }
    }

    return next_id;
}

static void sort_ports(in_port_t *ports, int count) {
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (ports[j] < ports[i]) {
                in_port_t tmp = ports[i];
                ports[i] = ports[j];
                ports[j] = tmp;
            }
        }
    }
}

void database_init(void) {
    static const char *initial_cards[] = {
        "Preparare la struttura della lavagna kanban",
        "Definire le colonne To Do, Doing e Done",
        "Stabilire il formato binario dei messaggi",
        "Registrare gli utenti tramite HELLO",
        "Mantenere aggiornata la lista degli utenti attivi",
        "Inviare AVAILABLE_CARD quando ci sono almeno due utenti",
        "Condividere la lista dei peer con SEND_USER_LIST",
        "Scambiare i costi di esecuzione con CHOOSE_USER",
        "Assegnare la card all'utente con costo minore",
        "Spostare la card in Doing dopo ACK_CARD",
        "Simulare l'esecuzione dell'attivita assegnata",
        "Completare la card con CARD_DONE",
        "Gestire QUIT e rimettere la card in To Do",
        "Controllare gli utenti attivi con PING_USER",
        "Aggiornare la stampa della lavagna dopo ogni modifica"
    };

    memset(&database, 0, sizeof(database));
    database.lavagna_id = 1;
    database.offered_card_id = -1;

    for (size_t i = 0; i < sizeof(initial_cards) / sizeof(initial_cards[0]); i++) {
        (void)database_create_card(0, CARD_STATUS_TODO, initial_cards[i]);
    }
}

void database_cleanup(void) {
    memset(&database, 0, sizeof(database));
    database.offered_card_id = -1;
}

int database_add_user(int socket_fd, in_port_t port) {
    int index;

    if (find_user_index(port) >= 0 || port < MIN_USER_PORT) {
        return -1;
    }

    index = find_free_user_index();
    if (index < 0) {
        return -1;
    }

    database.users[index].used = true;
    database.users[index].socket_fd = socket_fd;
    database.users[index].port = port;
    database.users[index].status = USER_STATUS_IDLE;
    database.users[index].assigned_card_id = -1;
    database.users[index].last_ping_timestamp = 0;
    database.num_users++;

    return 0;
}

int database_remove_user(in_port_t port) {
    int index = find_user_index(port);
    if (index < 0) {
        return -1;
    }

    memset(&database.users[index], 0, sizeof(database.users[index]));
    database.users[index].assigned_card_id = -1;
    database.num_users--;

    return 0;
}

int database_get_num_users(void) {
    return database.num_users;
}

int database_get_users_list(in_port_t **user_ports) {
    int count = database.num_users;
    int cursor = 0;

    if (count <= 0) {
        *user_ports = NULL;
        return 0;
    }

    *user_ports = malloc((size_t)count * sizeof(**user_ports));
    if (*user_ports == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_USERS; i++) {
        if (database.users[i].used) {
            (*user_ports)[cursor++] = database.users[i].port;
        }
    }

    sort_ports(*user_ports, count);
    return count;
}

int database_get_users_list_except(in_port_t excluded_port, in_port_t **user_ports) {
    int count = 0;
    int cursor = 0;

    for (int i = 0; i < MAX_USERS; i++) {
        if (database.users[i].used && database.users[i].port != excluded_port) {
            count++;
        }
    }

    if (count <= 0) {
        *user_ports = NULL;
        return 0;
    }

    *user_ports = malloc((size_t)count * sizeof(**user_ports));
    if (*user_ports == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_USERS; i++) {
        if (database.users[i].used && database.users[i].port != excluded_port) {
            (*user_ports)[cursor++] = database.users[i].port;
        }
    }

    sort_ports(*user_ports, count);
    return count;
}

in_port_t database_get_port_from_socket(int socket_fd) {
    int index = find_user_index_by_socket(socket_fd);
    if (index < 0) {
        return 0;
    }
    return database.users[index].port;
}

int database_get_socket_from_port(in_port_t port) {
    int index = find_user_index(port);
    if (index < 0) {
        return -1;
    }
    return database.users[index].socket_fd;
}

enum UserStatus database_user_get_status(in_port_t port) {
    int index = find_user_index(port);
    if (index < 0) {
        return USER_STATUS_EMPTY;
    }
    return database.users[index].status;
}

int database_user_set_status(in_port_t port, enum UserStatus status) {
    int index = find_user_index(port);
    if (index < 0) {
        return -1;
    }

    database.users[index].status = status;
    return 0;
}

int database_user_assign_card(in_port_t port, int card_id) {
    int index = find_user_index(port);
    if (index < 0 || find_card_index(card_id) < 0) {
        return -1;
    }

    database.users[index].assigned_card_id = card_id;
    database.users[index].status = USER_STATUS_ACTIVE;
    return 0;
}

int database_user_clear_card(in_port_t port) {
    int index = find_user_index(port);
    if (index < 0) {
        return -1;
    }

    database.users[index].assigned_card_id = -1;
    database.users[index].status = USER_STATUS_IDLE;
    database.users[index].last_ping_timestamp = 0;
    return 0;
}

int database_user_set_ping(in_port_t port) {
    int index = find_user_index(port);
    if (index < 0) {
        return -1;
    }

    database.users[index].status = USER_STATUS_PINGED;
    database.users[index].last_ping_timestamp = time(NULL);
    return 0;
}

int database_user_clear_ping(in_port_t port) {
    int index = find_user_index(port);
    if (index < 0) {
        return -1;
    }

    database.users[index].status = USER_STATUS_ACTIVE;
    database.users[index].last_ping_timestamp = 0;
    return 0;
}

int database_user_get_timed_out(in_port_t **user_ports, int timeout) {
    int count = 0;
    time_t now = time(NULL);

    *user_ports = malloc((size_t)MAX_USERS * sizeof(**user_ports));
    if (*user_ports == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_USERS; i++) {
        if (database.users[i].used && database.users[i].status == USER_STATUS_PINGED) {
            if (difftime(now, database.users[i].last_ping_timestamp) >= timeout) {
                (*user_ports)[count++] = database.users[i].port;
            }
        }
    }

    return count;
}

void database_print_users(void) {
    fprintf(stdout, "==== Utenti registrati (%d) ====\n", database.num_users);
    if (database.num_users == 0) {
        fprintf(stdout, "(nessun utente)\n");
    }

    for (int i = 0; i < MAX_USERS; i++) {
        if (database.users[i].used) {
            fprintf(stdout, "Porta: %u  Socket: %d  Stato: %s  Card: %d\n",
                    (unsigned)database.users[i].port,
                    database.users[i].socket_fd,
                    USER_STATUS_TEXT[database.users[i].status],
                    database.users[i].assigned_card_id);
        }
    }
    fprintf(stdout, "================================\n");
    fflush(stdout);
}

int database_create_card(int requested_id, enum CardStatus status, const char *text) {
    int index;
    int card_id = requested_id;

    if (text == NULL || text[0] == '\0') {
        return -1;
    }
    if (card_id <= 0) {
        card_id = next_card_id();
    }
    if (find_card_index(card_id) >= 0) {
        return -1;
    }

    index = find_free_card_index();
    if (index < 0) {
        return -1;
    }

    database.cards[index].used = true;
    database.cards[index].id = card_id;
    database.cards[index].status = status;
    snprintf(database.cards[index].text, sizeof(database.cards[index].text), "%s", text);
    database.cards[index].assigned_user_port = 0;
    database.cards[index].last_activity_timestamp = time(NULL);
    database.num_cards++;

    return card_id;
}

int database_get_next_todo_card(void) {
    int best_id = -1;

    for (int i = 0; i < MAX_CARDS; i++) {
        if (database.cards[i].used && database.cards[i].status == CARD_STATUS_TODO) {
            if (best_id == -1 || database.cards[i].id < best_id) {
                best_id = database.cards[i].id;
            }
        }
    }

    return best_id;
}

int database_get_offered_card(void) {
    return database.offered_card_id;
}

void database_set_offered_card(int card_id) {
    database.offered_card_id = card_id;
}

void database_clear_offered_card(void) {
    database.offered_card_id = -1;
}

bool database_has_doing_cards(void) {
    for (int i = 0; i < MAX_CARDS; i++) {
        if (database.cards[i].used && database.cards[i].status == CARD_STATUS_DOING) {
            return true;
        }
    }

    return false;
}

int database_card_get_text(int card_id, char *buffer, size_t buffer_size) {
    int index = find_card_index(card_id);
    if (index < 0 || buffer == NULL || buffer_size == 0) {
        return -1;
    }

    snprintf(buffer, buffer_size, "%s", database.cards[index].text);
    return (int)strlen(buffer);
}

enum CardStatus database_card_get_status(int card_id) {
    int index = find_card_index(card_id);
    if (index < 0) {
        return CARD_STATUS_TODO;
    }
    return database.cards[index].status;
}

in_port_t database_card_get_user(int card_id) {
    int index = find_card_index(card_id);
    if (index < 0) {
        return 0;
    }
    return database.cards[index].assigned_user_port;
}

int database_card_doing(in_port_t port, int card_id) {
    int index = find_card_index(card_id);
    int user_index = find_user_index(port);

    if (index < 0 || user_index < 0 || database.cards[index].status != CARD_STATUS_TODO) {
        return -1;
    }
    if (database.users[user_index].status != USER_STATUS_IDLE) {
        return -1;
    }
    if (database_user_assign_card(port, card_id) < 0) {
        return -1;
    }

    database.cards[index].status = CARD_STATUS_DOING;
    database.cards[index].assigned_user_port = port;
    database.cards[index].last_activity_timestamp = time(NULL);
    return 0;
}

int database_card_done(in_port_t port, int card_id) {
    int index = find_card_index(card_id);

    if (index < 0 || database.cards[index].status != CARD_STATUS_DOING) {
        return -1;
    }
    if (database.cards[index].assigned_user_port != port) {
        return -1;
    }

    database.cards[index].status = CARD_STATUS_DONE;
    database.cards[index].last_activity_timestamp = time(NULL);
    database.cards[index].assigned_user_port = port;
    (void)database_user_clear_card(port);
    return 0;
}

int database_card_todo(in_port_t port) {
    int user_index = find_user_index(port);
    if (user_index < 0) {
        return 0;
    }

    int card_id = database.users[user_index].assigned_card_id;
    if (card_id < 0) {
        return 0;
    }

    int card_index = find_card_index(card_id);
    if (card_index >= 0 && database.cards[card_index].status == CARD_STATUS_DOING) {
        database.cards[card_index].status = CARD_STATUS_TODO;
        database.cards[card_index].assigned_user_port = 0;
        database.cards[card_index].last_activity_timestamp = time(NULL);
    }

    (void)database_user_clear_card(port);
    return 0;
}

int database_move_card(int card_id, enum CardStatus status, in_port_t port) {
    int index = find_card_index(card_id);
    if (index < 0) {
        return -1;
    }

    if (status == CARD_STATUS_TODO) {
        if (database.cards[index].assigned_user_port != 0) {
            (void)database_user_clear_card(database.cards[index].assigned_user_port);
        }
        database.cards[index].assigned_user_port = 0;
    } else {
        if (port == 0 || database_user_assign_card(port, card_id) < 0) {
            return -1;
        }
        database.cards[index].assigned_user_port = port;
    }

    database.cards[index].status = status;
    database.cards[index].last_activity_timestamp = time(NULL);
    return 0;
}

int database_card_reset_timestamp(in_port_t port) {
    int user_index = find_user_index(port);
    if (user_index < 0) {
        return -1;
    }

    int card_index = find_card_index(database.users[user_index].assigned_card_id);
    if (card_index < 0 || database.cards[card_index].status != CARD_STATUS_DOING) {
        return -1;
    }

    database.cards[card_index].last_activity_timestamp = time(NULL);
    return 0;
}

int database_card_get_timed_out(int **card_ids, int timeout) {
    int count = 0;
    time_t now = time(NULL);

    *card_ids = malloc((size_t)MAX_CARDS * sizeof(**card_ids));
    if (*card_ids == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_CARDS; i++) {
        if (database.cards[i].used && database.cards[i].status == CARD_STATUS_DOING) {
            if (difftime(now, database.cards[i].last_activity_timestamp) >= timeout) {
                (*card_ids)[count++] = database.cards[i].id;
            }
        }
    }

    return count;
}

static void print_cards_in_status(enum CardStatus status) {
    bool empty = true;

    for (int i = 0; i < MAX_CARDS; i++) {
        if (database.cards[i].used && database.cards[i].status == status) {
            empty = false;
            fprintf(stdout, "Card ID: %d  User: %u  Timestamp: %ld  Testo: %s\n",
                    database.cards[i].id,
                    (unsigned)database.cards[i].assigned_user_port,
                    (long)database.cards[i].last_activity_timestamp,
                    database.cards[i].text);
        }
    }

    if (empty) {
        fprintf(stdout, "(vuota)\n");
    }
}

void database_print_cards(void) {
    fprintf(stdout, "\n================ Lavagna %d ================\n", database.lavagna_id);
    for (int status = CARD_STATUS_TODO; status <= CARD_STATUS_DONE; status++) {
        fprintf(stdout, "-- %s --\n", CARD_STATUS_TEXT[status]);
        print_cards_in_status((enum CardStatus)status);
    }
    fprintf(stdout, "===========================================\n\n");
    fflush(stdout);
}
