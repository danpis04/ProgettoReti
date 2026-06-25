#ifndef THREAD_H
#define THREAD_H

#include "common.h"

// Entry point dei thread secondari dell'utente.
void *p2p_server_function(void *arg);
void *worker_thread_function(void *arg);

#endif
