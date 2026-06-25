CC      := gcc
CFLAGS  := -Wall -Wextra -Wpedantic 

LDLIBS  := -pthread

COMMON_SRC := src/common/server.c src/common/protocol.c src/common/client.c
LAVAGNA_SRC := src/lavagna/database.c src/lavagna/lavagna_utils.c
UTENTE_SRC := src/utente/utente_state.c src/utente/utente_utils.c src/utente/p2p_utils.c src/utente/p2p_thread.c src/utente/worker_thread.c

COMMON_OBJ := $(COMMON_SRC:.c=.o)
LAVAGNA_OBJ := $(LAVAGNA_SRC:.c=.o)
UTENTE_OBJ := $(UTENTE_SRC:.c=.o)

HEADERS := include/common.h include/protocol.h include/server.h include/client.h include/database.h include/lavagna_utils.h include/utente_state.h include/p2p_utils.h include/utente_utils.h include/thread.h

TARGETS := lavagna utente

all: $(TARGETS)

lavagna: lavagna.o $(COMMON_OBJ) $(LAVAGNA_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

utente: utente.o $(COMMON_OBJ) $(UTENTE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

run-lavagna: lavagna
	./lavagna

run-utente: utente
	./utente

run-server: run-lavagna

run-client: run-utente

clean:
	rm -f $(TARGETS) lavagna.o utente.o $(COMMON_OBJ) $(LAVAGNA_OBJ) $(UTENTE_OBJ)

rebuild: clean all

.PHONY: all clean rebuild run-lavagna run-utente run-server run-client
