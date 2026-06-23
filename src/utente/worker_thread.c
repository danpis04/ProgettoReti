#include "../../include/thread.h"
#include "../../include/utente_state.h"

void *worker_thread_function(void *arg) {
    (void)arg;

    int card_id = utente_get_card_id();
    int sleep_seconds = 1 + (utente_get_port() % 3);

    fprintf(stdout, "Lavoro avviato sulla card %d per %d secondi\n", card_id, sleep_seconds);
    fflush(stdout);
    sleep((unsigned int)sleep_seconds);
    fprintf(stdout, "Lavoro completato sulla card %d\n", card_id);
    fflush(stdout);

    utente_mark_done_pending();
    return NULL;
}
