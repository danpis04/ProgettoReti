#ifndef COMMON_H
#define COMMON_H

/* Header condiviso per librerie standard/POSIX usate dai moduli del progetto. */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define LOCALHOST "127.0.0.1"
#define SERVER_PORT 5678
#define MIN_USER_PORT 5679

#define MAX_USERS 50
#define MAX_CARDS 128
#define CARD_TEXT_SIZE 256
#define MAX_PAYLOAD_SIZE 1024

#define WORKING_TIMEOUT_SECONDS 12
#define PING_TIMEOUT_SECONDS 3
#define TIMEOUT_CHECK_PERIOD 1

#endif
