#include "../../include/utente_state.h"

static struct Utente utente;

static void reset_choices_locked(void) {
    for (int i = 0; i < MAX_USERS; i++) {
        utente.choices[i].used = false;
        utente.choices[i].port = 0;
        utente.choices[i].cost = 0;
    }
    utente.choice_count = 0;
}

static void add_choice_locked(in_port_t port, int cost) {
    for (int i = 0; i < MAX_USERS; i++) {
        if (utente.choices[i].used && utente.choices[i].port == port) {
            return;
        }
    }

    for (int i = 0; i < MAX_USERS; i++) {
        if (!utente.choices[i].used) {
            utente.choices[i].used = true;
            utente.choices[i].port = port;
            utente.choices[i].cost = cost;
            utente.choice_count++;
            return;
        }
    }
}

static int choose_winner_locked(void) {
    int best_cost = 0;
    in_port_t best_port = 0;
    bool found = false;

    for (int i = 0; i < MAX_USERS; i++) {
        if (!utente.choices[i].used) {
            continue;
        }

        if (!found
                || utente.choices[i].cost < best_cost
                || (utente.choices[i].cost == best_cost && utente.choices[i].port < best_port)) {
            found = true;
            best_cost = utente.choices[i].cost;
            best_port = utente.choices[i].port;
        }
    }

    return found ? (int)best_port : -1;
}

static void evaluate_election_locked(void) {
    int winner;

    if (utente.state != STATE_CHOOSING || utente.total_users <= 0
            || utente.choice_count < utente.total_users) {
        return;
    }

    winner = choose_winner_locked();
    fprintf(stdout, "CHOOSE_USER completato per card %d: vincitore %d\n",
            utente.card_id, winner);
    fflush(stdout);

    if (winner == (int)utente.port) {
        utente.state = STATE_ACK_PENDING;
    } else {
        utente.state = STATE_IDLE;
        utente.card_id = -1;
        utente.total_users = 0;
        utente.peer_count = 0;
        reset_choices_locked();
    }
}

void utente_init(in_port_t port) {
    memset(&utente, 0, sizeof(utente));
    utente.port = port;
    utente.state = STATE_STARTING_P2P;
    utente.card_id = -1;
    srand(time(NULL));
    for (in_port_t current_port = MIN_USER_PORT; current_port < port; current_port++) {
        (void)rand();
    }
    pthread_mutex_init(&utente.mutex, NULL);
}

void utente_cleanup(void) {
    pthread_mutex_destroy(&utente.mutex);
}

in_port_t utente_get_port(void) {
    return utente.port;
}

enum UtenteState utente_get_state(void) {
    enum UtenteState state;

    pthread_mutex_lock(&utente.mutex);
    state = utente.state;
    pthread_mutex_unlock(&utente.mutex);

    return state;
}

void utente_update_state(enum UtenteState state) {
    pthread_mutex_lock(&utente.mutex);
    utente.state = state;
    pthread_mutex_unlock(&utente.mutex);
}

int utente_get_card_id(void) {
    int card_id;

    pthread_mutex_lock(&utente.mutex);
    card_id = utente.card_id;
    pthread_mutex_unlock(&utente.mutex);

    return card_id;
}

int utente_get_own_cost(void) {
    int cost;

    pthread_mutex_lock(&utente.mutex);
    cost = utente.own_cost;
    pthread_mutex_unlock(&utente.mutex);

    return cost;
}

void utente_start_election(int card_id, const char *text, int total_users,
                           const in_port_t *peers, int peer_count, int own_cost) {
    pthread_mutex_lock(&utente.mutex);

    if (utente.card_id != card_id) {
        reset_choices_locked();
    }

    utente.card_id = card_id;
    snprintf(utente.card_text, sizeof(utente.card_text), "%s", text);
    utente.total_users = total_users;
    utente.peer_count = peer_count > MAX_USERS ? MAX_USERS : peer_count;
    for (int i = 0; i < utente.peer_count; i++) {
        utente.peer_ports[i] = peers[i];
    }
    utente.own_cost = own_cost;
    utente.state = STATE_CHOOSING;
    add_choice_locked(utente.port, own_cost);

    fprintf(stdout, "AVAILABLE_CARD ricevuto: card %d, costo locale %d, utenti %d\n",
            card_id, own_cost, total_users);
    fflush(stdout);

    evaluate_election_locked();
    pthread_mutex_unlock(&utente.mutex);
}

void utente_record_choice(int card_id, in_port_t port, int cost) {
    pthread_mutex_lock(&utente.mutex);

    if (utente.card_id == -1) {
        utente.card_id = card_id;
    }
    if (utente.card_id == card_id) {
        add_choice_locked(port, cost);
        fprintf(stdout, "CHOOSE_USER ricevuto da %u con costo %d per card %d\n",
                (unsigned)port, cost, card_id);
        fflush(stdout);
        evaluate_election_locked();
    }

    pthread_mutex_unlock(&utente.mutex);
}

bool utente_take_ack_action(int *card_id) {
    bool ready = false;

    pthread_mutex_lock(&utente.mutex);
    if (utente.state == STATE_ACK_PENDING) {
        *card_id = utente.card_id;
        ready = true;
    }
    pthread_mutex_unlock(&utente.mutex);

    return ready;
}

bool utente_take_manual_ack_action(int requested_card_id, int *card_id) {
    bool ready = false;

    pthread_mutex_lock(&utente.mutex);
    if ((utente.state == STATE_CHOOSING || utente.state == STATE_ACK_PENDING)
            && utente.card_id != -1
            && (requested_card_id <= 0 || requested_card_id == utente.card_id)) {
        *card_id = utente.card_id;
        utente.state = STATE_WORKING;
        ready = true;
    }
    pthread_mutex_unlock(&utente.mutex);

    return ready;
}

void utente_mark_working(void) {
    pthread_mutex_lock(&utente.mutex);
    utente.state = STATE_WORKING;
    pthread_mutex_unlock(&utente.mutex);
}

void utente_mark_done_pending(int card_id) {
    pthread_mutex_lock(&utente.mutex);
    if (utente.state == STATE_WORKING && utente.card_id == card_id) {
        utente.state = STATE_DONE_PENDING;
    }
    pthread_mutex_unlock(&utente.mutex);
}

bool utente_take_done_action(int *card_id) {
    bool ready = false;

    pthread_mutex_lock(&utente.mutex);
    if (utente.state == STATE_DONE_PENDING) {
        *card_id = utente.card_id;
        utente.state = STATE_IDLE;
        utente.card_id = -1;
        utente.total_users = 0;
        utente.peer_count = 0;
        reset_choices_locked();
        ready = true;
    }
    pthread_mutex_unlock(&utente.mutex);

    return ready;
}

bool utente_take_manual_done_action(int requested_card_id, int *card_id) {
    bool ready = false;

    pthread_mutex_lock(&utente.mutex);
    if ((utente.state == STATE_WORKING || utente.state == STATE_DONE_PENDING)
            && utente.card_id != -1
            && (requested_card_id <= 0 || requested_card_id == utente.card_id)) {
        *card_id = utente.card_id;
        utente.state = STATE_IDLE;
        utente.card_id = -1;
        utente.total_users = 0;
        utente.peer_count = 0;
        reset_choices_locked();
        if (utente.worker_started) {
            (void)pthread_detach(utente.worker_thread);
            utente.worker_started = false;
        }
        ready = true;
    }
    pthread_mutex_unlock(&utente.mutex);

    return ready;
}

int utente_copy_peers(in_port_t *ports, int max_ports) {
    int count;

    pthread_mutex_lock(&utente.mutex);
    count = utente.peer_count < max_ports ? utente.peer_count : max_ports;
    for (int i = 0; i < count; i++) {
        ports[i] = utente.peer_ports[i];
    }
    pthread_mutex_unlock(&utente.mutex);

    return count;
}

int utente_start_p2p(void *(*thread_function)(void *)) {
    if (pthread_create(&utente.p2p_thread, NULL, thread_function, NULL) != 0) {
        return -1;
    }
    return 0;
}

void utente_wait_for_p2p_shutdown(void) {
    pthread_join(utente.p2p_thread, NULL);
}

int utente_start_worker(void *(*thread_function)(void *)) {
    pthread_mutex_lock(&utente.mutex);
    if (utente.worker_started) {
        pthread_mutex_unlock(&utente.mutex);
        return -1;
    }
    utente.worker_started = true;
    pthread_mutex_unlock(&utente.mutex);

    if (pthread_create(&utente.worker_thread, NULL, thread_function, NULL) != 0) {
        pthread_mutex_lock(&utente.mutex);
        utente.worker_started = false;
        pthread_mutex_unlock(&utente.mutex);
        return -1;
    }
    return 0;
}

void utente_wait_for_worker_shutdown(void) {
    bool started;

    pthread_mutex_lock(&utente.mutex);
    started = utente.worker_started;
    pthread_mutex_unlock(&utente.mutex);

    if (started) {
        pthread_join(utente.worker_thread, NULL);
        pthread_mutex_lock(&utente.mutex);
        utente.worker_started = false;
        pthread_mutex_unlock(&utente.mutex);
    }
}
