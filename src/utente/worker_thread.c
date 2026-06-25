#include "../../include/thread.h"
#include "../../include/utente_state.h"

// Simula il lavoro sulla card assegnata e notifica il main thread a fine lavoro.
void *worker_thread_function(void *arg) {
    (void)arg;

    int card_id = utente_get_card_id();
    int sleep_seconds = rand() % 5 * 5 + 5;

    fprintf(stdout, "Lavoro avviato sulla card %d per %d secondi\n", card_id, sleep_seconds);
    fflush(stdout);
    sleep((unsigned int)sleep_seconds);
    fprintf(stdout, "Lavoro completato sulla card %d\n", card_id);
    fflush(stdout);

    utente_mark_done_pending(card_id);
    return NULL;
}
