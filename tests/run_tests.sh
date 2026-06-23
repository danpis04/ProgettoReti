#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORKDIR=""
PIDS=()

pass() {
    printf 'PASS %s\n' "$1"
}

fail() {
    printf 'FAIL %s\n' "$1" >&2
    cleanup
    exit 1
}

cleanup() {
    if [ "${#PIDS[@]}" -gt 0 ]; then
        kill -TERM "${PIDS[@]}" 2>/dev/null || true
        wait "${PIDS[@]}" 2>/dev/null || true
    fi
    PIDS=()
}

start_lavagna() {
    local log_file="$1"
    "$ROOT/lavagna" < /dev/null > "$log_file" 2>&1 &
    PIDS+=("$!")
    sleep 1
}

start_utente() {
    local stdin_file="$1"
    local log_file="$2"
    "$ROOT/utente" < "$stdin_file" > "$log_file" 2>&1 &
    PIDS+=("$!")
}

start_utente_on_port() {
    local port="$1"
    local stdin_file="$2"
    local log_file="$3"
    "$ROOT/utente" "$port" < "$stdin_file" > "$log_file" 2>&1 &
    PIDS+=("$!")
}

wait_for_log() {
    local description="$1"
    local timeout_seconds="$2"
    local pattern="$3"
    local file="$4"

    for _ in $(seq 1 "$timeout_seconds"); do
        if grep -q "$pattern" "$file" 2>/dev/null; then
            pass "$description"
            return 0
        fi
        sleep 1
    done

    fail "$description"
}

wait_for_count() {
    local description="$1"
    local timeout_seconds="$2"
    local pattern="$3"
    local file="$4"
    local expected="$5"

    for _ in $(seq 1 "$timeout_seconds"); do
        local count
        count="$(grep -c "$pattern" "$file" 2>/dev/null || true)"
        if [ "$count" -ge "$expected" ]; then
            pass "$description"
            return 0
        fi
        sleep 1
    done

    fail "$description"
}

build_project() {
    local log_file="$WORKDIR/build.log"
    make -C "$ROOT" clean all > "$log_file" 2>&1 || fail "compilazione progetto"
    if grep -i 'warning:' "$log_file" >/dev/null 2>&1; then
        fail "compilazione senza warning"
    fi
    pass "compilazione senza warning"
}

check_invalid_port() {
    if "$ROOT/utente" 5600 > "$WORKDIR/invalid_port.log" 2>&1; then
        fail "rifiuto porta utente non valida"
    fi
    pass "rifiuto porta utente non valida"
}

check_duplicate_explicit_port() {
    local lavagna_log="$WORKDIR/duplicate_lavagna.log"
    local u1_log="$WORKDIR/duplicate_u1.log"
    local u2_log="$WORKDIR/duplicate_u2.log"

    cleanup
    start_lavagna "$lavagna_log"
    start_utente_on_port 5684 /dev/null "$u1_log"
    wait_for_log "primo utente esplicito su 5684" 10 'Utente avviato sulla porta 5684' "$u1_log"

    if "$ROOT/utente" 5684 < /dev/null > "$u2_log" 2>&1; then
        fail "rifiuto porta esplicita gia' occupata"
    fi

    pass "rifiuto porta esplicita gia' occupata"
    cleanup
}

check_explicit_port() {
    local lavagna_log="$WORKDIR/explicit_lavagna.log"
    local u1_log="$WORKDIR/explicit_u1.log"

    cleanup
    start_lavagna "$lavagna_log"
    start_utente_on_port 5684 /dev/null "$u1_log"
    wait_for_log "utente con porta esplicita 5684" 10 'Utente avviato sulla porta 5684' "$u1_log"
    cleanup
}

check_automatic_ports() {
    local lavagna_log="$WORKDIR/auto_lavagna.log"
    local u1_log="$WORKDIR/auto_u1.log"
    local u2_log="$WORKDIR/auto_u2.log"

    cleanup
    start_lavagna "$lavagna_log"
    start_utente /dev/null "$u1_log"
    wait_for_log "primo utente su porta 5679" 10 'Utente avviato sulla porta 5679' "$u1_log"
    start_utente /dev/null "$u2_log"
    wait_for_log "secondo utente su porta 5680" 10 'Utente avviato sulla porta 5680' "$u2_log"
    sleep 1

    cleanup
}

check_user_count_range_1_to_50() {
    local lavagna_log="$WORKDIR/range_lavagna.log"

    cleanup
    start_lavagna "$lavagna_log"

    for i in $(seq 0 49); do
        local port=$((5679 + i))
        local log_file="$WORKDIR/range_u${port}.log"

        start_utente /dev/null "$log_file"
        wait_for_log "utente automatico su porta $port" 15 "Utente avviato sulla porta $port" "$log_file"
    done

    for count in $(seq 1 50); do
        wait_for_log "conteggio utenti $count/50" 15 "Utenti registrati ($count)" "$lavagna_log"
    done

    cleanup
}

check_user_churn() {
    local lavagna_log="$WORKDIR/churn_lavagna.log"
    local u1_fifo="$WORKDIR/churn_u1.in"
    local u2_fifo="$WORKDIR/churn_u2.in"

    cleanup
    mkfifo "$u1_fifo" "$u2_fifo"
    exec 7<>"$u1_fifo"
    exec 8<>"$u2_fifo"

    start_lavagna "$lavagna_log"
    for i in $(seq 5681 5684); do
        start_utente_on_port "$i" /dev/null "$WORKDIR/churn_u${i}.log"
    done
    start_utente_on_port 5685 "$u1_fifo" "$WORKDIR/churn_u5685.log"
    start_utente_on_port 5686 "$u2_fifo" "$WORKDIR/churn_u5686.log"

    wait_for_log "sei utenti registrati" 15 'Utenti registrati (6)' "$lavagna_log"

    printf 'QUIT\n' >&7
    printf 'QUIT\n' >&8
    wait_for_log "uscita primo utente churn" 15 "QUIT ricevuto dall'utente 5685" "$lavagna_log"
    wait_for_log "uscita secondo utente churn" 15 "QUIT ricevuto dall'utente 5686" "$lavagna_log"
    wait_for_log "quattro utenti dopo uscite" 15 'Utenti registrati (4)' "$lavagna_log"

    start_utente_on_port 5687 /dev/null "$WORKDIR/churn_new1.log"
    start_utente_on_port 5688 /dev/null "$WORKDIR/churn_new2.log"
    wait_for_log "rientro nuovo utente" 15 'Utenti registrati (5)' "$lavagna_log"
    wait_for_log "rientro secondo nuovo utente" 15 'Utenti registrati (6)' "$lavagna_log"

    exec 7>&-
    exec 8>&-
    cleanup
}

check_full_even_flow() {
    local lavagna_log="$WORKDIR/full_lavagna.log"

    cleanup
    start_lavagna "$lavagna_log"
    start_utente /dev/null "$WORKDIR/full_u5679.log"
    start_utente /dev/null "$WORKDIR/full_u5680.log"
    start_utente /dev/null "$WORKDIR/full_u5681.log"
    start_utente /dev/null "$WORKDIR/full_u5682.log"

    wait_for_count "10 card completate" 70 'CARD_DONE ricevuto' "$lavagna_log" 10
    wait_for_count "10 card prese in carico" 5 'ACK_CARD ricevuto' "$lavagna_log" 10
    wait_for_count "10 offerte AVAILABLE_CARD" 5 'AVAILABLE_CARD inviato' "$lavagna_log" 10
    wait_for_log "colonna Done contiene card 10" 5 'Card ID: 10' "$lavagna_log"
    wait_for_log "negoziazione CHOOSE_USER eseguita" 5 'CHOOSE_USER completato' "$WORKDIR/full_u5679.log"
    cleanup
}

check_terminal_commands() {
    local lavagna_log="$WORKDIR/commands_lavagna.log"
    local u1_fifo="$WORKDIR/u1.in"
    local u2_fifo="$WORKDIR/u2.in"

    cleanup
    mkfifo "$u1_fifo" "$u2_fifo"
    exec 7<>"$u1_fifo"
    exec 8<>"$u2_fifo"

    start_lavagna "$lavagna_log"
    start_utente "$u1_fifo" "$WORKDIR/commands_u5679.log"
    start_utente "$u2_fifo" "$WORKDIR/commands_u5680.log"
    sleep 2

    local u1_port
    u1_port="$(grep 'Utente avviato sulla porta' "$WORKDIR/commands_u5679.log" | head -1 | sed -E 's/.*porta ([0-9]+).*/\1/')"
    if [ -z "$u1_port" ]; then
        fail "porta primo utente comandi"
    fi

    printf 'SEND_USER_LIST\n' >&7
    wait_for_log "SEND_USER_LIST ricevuta dall'utente" 10 'Lista utenti ricevuta' "$WORKDIR/commands_u5679.log"

    printf 'CREATE_CARD 77 TODO Card creata da comando terminale\n' >&7
    wait_for_log "CREATE_CARD da utente accettata" 10 'Card 77 creata' "$lavagna_log"

    printf 'QUIT\n' >&7
    wait_for_log "QUIT da utente registrato" 10 "QUIT ricevuto dall'utente $u1_port" "$lavagna_log"

    exec 7>&-
    exec 8>&-
    cleanup
}

check_lavagna_commands_and_ping() {
    local lavagna_log="$WORKDIR/cmd_lavagna.log"
    local lavagna_fifo="$WORKDIR/lavagna.in"
    local u1_fifo="$WORKDIR/cmd_u1.in"
    local u2_fifo="$WORKDIR/cmd_u2.in"

    cleanup
    mkfifo "$lavagna_fifo" "$u1_fifo" "$u2_fifo"
    exec 9<>"$lavagna_fifo"
    exec 7<>"$u1_fifo"
    exec 8<>"$u2_fifo"

    "$ROOT/lavagna" < "$lavagna_fifo" > "$lavagna_log" 2>&1 &
    PIDS+=("$!")
    sleep 1

    start_utente_on_port 5681 "$u1_fifo" "$WORKDIR/cmd_u5681.log"
    start_utente_on_port 5684 "$u2_fifo" "$WORKDIR/cmd_u5684.log"

    wait_for_log "negoziazione disponibile" 15 'AVAILABLE_CARD inviato' "$lavagna_log"
    wait_for_log "presa in carico card" 15 'ACK_CARD ricevuto da' "$lavagna_log"

    local winner
    winner="$(grep 'ACK_CARD ricevuto da' "$lavagna_log" | tail -1 | sed -E 's/.*da ([0-9]+) per.*/\1/')"
    printf 'PING_USER %s\n' "$winner" >&9
    wait_for_log "ping/pong utente attivo" 15 "PONG_LAVAGNA ricevuto dall'utente $winner" "$lavagna_log"

    printf 'SHOW_UTENTI\n' >&9
    wait_for_log "SHOW_UTENTI da lavagna" 10 'Utenti registrati' "$lavagna_log"

    printf 'SHOW_LAVAGNA\n' >&9
    wait_for_log "SHOW_LAVAGNA da lavagna" 10 'Lavagna 1' "$lavagna_log"

    printf 'CREATE_CARD 88 TODO Card creata dalla lavagna\n' >&9
    wait_for_log "CREATE_CARD da lavagna" 10 'Card 88 creata' "$lavagna_log"

    printf 'MOVE_CARD 88 TODO\n' >&9
    wait_for_log "MOVE_CARD da lavagna" 10 'Card ID: 88' "$lavagna_log"

    printf 'SEND_USER_LIST 5684\n' >&9
    wait_for_log "SEND_USER_LIST da lavagna" 10 'Lista utenti ricevuta' "$WORKDIR/cmd_u5684.log"

    exec 7>&-
    exec 8>&-
    exec 9>&-
    cleanup
}

main() {
    WORKDIR="$(mktemp -d /tmp/progetto_reti_tests.XXXXXX)"
    trap cleanup EXIT

    build_project
    check_invalid_port
    check_explicit_port
    check_duplicate_explicit_port
    check_automatic_ports
    check_user_count_range_1_to_50
    check_user_churn
    check_full_even_flow
    check_terminal_commands
    check_lavagna_commands_and_ping

    printf 'TUTTI I CONTROLLI SONO PASSATI\n'
}

main "$@"
