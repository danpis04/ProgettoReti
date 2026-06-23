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

    printf 'SEND_USER_LIST\n' >&7
    wait_for_log "SEND_USER_LIST ricevuta dall'utente" 10 'Lista utenti ricevuta' "$WORKDIR/commands_u5679.log"

    printf 'CREATE_CARD 77 TODO Card creata da comando terminale\n' >&7
    wait_for_log "CREATE_CARD da utente accettata" 10 'Card 77 creata' "$lavagna_log"

    printf 'QUIT\n' >&7
    wait_for_log "QUIT da utente registrato" 10 'QUIT ricevuto dall'"'"'utente 5679' "$lavagna_log"

    exec 7>&-
    exec 8>&-
    cleanup
}

main() {
    WORKDIR="$(mktemp -d /tmp/progetto_reti_tests.XXXXXX)"
    trap cleanup EXIT

    build_project
    check_invalid_port
    check_explicit_port
    check_automatic_ports
    check_full_even_flow
    check_terminal_commands

    printf 'TUTTI I CONTROLLI SONO PASSATI\n'
}

main "$@"
