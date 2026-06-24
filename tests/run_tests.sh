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

drop_pid() {
    local pid="$1"
    local remaining=()

    for current_pid in "${PIDS[@]}"; do
        if [ "$current_pid" != "$pid" ]; then
            remaining+=("$current_pid")
        fi
    done

    PIDS=("${remaining[@]}")
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

assert_log_absent() {
    local description="$1"
    local pattern="$2"
    local file="$3"

    if grep -q "$pattern" "$file" 2>/dev/null; then
        fail "$description"
    fi
    pass "$description"
}

send_raw_protocol_message() {
    local msg_type="$1"
    local payload_length="$2"
    local payload_hex="${3:-}"

    python3 - "$msg_type" "$payload_length" "$payload_hex" <<'PY'
import socket
import struct
import sys

msg_type = int(sys.argv[1])
payload_length = int(sys.argv[2])
payload = bytes.fromhex(sys.argv[3])

with socket.create_connection(("127.0.0.1", 5678), timeout=3) as sock:
    sock.sendall(struct.pack("!II", msg_type, payload_length))
    if payload:
        sock.sendall(payload)
PY
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
    local lavagna_fifo="$WORKDIR/range_lavagna.in"
    local first_user_fifo="$WORKDIR/range_u5679.in"

    cleanup
    mkfifo "$lavagna_fifo" "$first_user_fifo"
    exec 9<>"$lavagna_fifo"
    exec 8<>"$first_user_fifo"

    "$ROOT/lavagna" < "$lavagna_fifo" > "$lavagna_log" 2>&1 &
    PIDS+=("$!")
    sleep 1

    for i in $(seq 0 49); do
        local port=$((5679 + i))
        local log_file="$WORKDIR/range_u${port}.log"
        local utente_card=$((9100 + i))

        if [ "$i" -eq 0 ]; then
            start_utente "$first_user_fifo" "$log_file"
        else
            start_utente /dev/null "$log_file"
        fi
        wait_for_log "utente automatico su porta $port" 15 "Utente avviato sulla porta $port" "$log_file"
        wait_for_log "conteggio utenti $((i + 1))/50" 15 "Utenti registrati ($((i + 1)))" "$lavagna_log"

        printf 'SHOW_UTENTI\n' >&9
        sleep 0.1
        printf 'SHOW_LAVAGNA\n' >&9
        sleep 0.2
        printf 'SEND_USER_LIST 5679\n' >&9
        sleep 0.2
        wait_for_count "SEND_USER_LIST conteggio $((i + 1))/50" 10 'Lista utenti ricevuta' "$WORKDIR/range_u5679.log" $((i + 1))
        printf 'CREATE_CARD %d DONE Range utente %d\n' "$utente_card" "$((i + 1))" >&8
        wait_for_log "CREATE_CARD utente conteggio $((i + 1))/50" 10 "Card $utente_card creata" "$lavagna_log"
    done

    wait_for_count "SEND_USER_LIST per conteggi 1..50" 20 'Lista utenti ricevuta' "$WORKDIR/range_u5679.log" 50
    wait_for_log "CREATE_CARD utente per conteggio 50" 20 'Card 9149 creata' "$lavagna_log"

    exec 8>&-
    exec 9>&-
    cleanup
}

check_single_user_behavior() {
    local lavagna_log="$WORKDIR/single_lavagna.log"

    cleanup
    start_lavagna "$lavagna_log"
    start_utente_on_port 5679 /dev/null "$WORKDIR/single_u5679.log"
    wait_for_log "un solo utente registrato" 10 'Utenti registrati (1)' "$lavagna_log"
    sleep 4

    assert_log_absent "nessuna offerta con un solo utente" 'AVAILABLE_CARD inviato' "$lavagna_log"
    assert_log_absent "nessuna presa in carico con un solo utente" 'ACK_CARD ricevuto' "$lavagna_log"

    cleanup
}

check_manual_ack_and_random_seed() {
    local fake_log="$WORKDIR/manual_ack_fake_lavagna.log"
    local user_log="$WORKDIR/manual_ack_user.log"
    local user_fifo="$WORKDIR/manual_ack_user.in"
    local cost_count
    local first_cost
    local second_cost

    cleanup
    mkfifo "$user_fifo"
    exec 7<>"$user_fifo"

    python3 - "$fake_log" <<'PY' &
import socket
import struct
import sys
import threading
import time

MSG_HELLO = 1
MSG_AVAILABLE_CARD = 10
MSG_CHOOSE_USER = 11
MSG_ACK_CARD = 12

log_path = sys.argv[1]

def log(text):
    with open(log_path, "a", encoding="utf-8") as output:
        output.write(text + "\n")
        output.flush()

def recv_all(conn, size):
    data = b""
    while len(data) < size:
        chunk = conn.recv(size - len(data))
        if not chunk:
            return None
        data += chunk
    return data

def recv_msg(conn):
    header = recv_all(conn, 8)
    if header is None:
        return None, None
    msg_type, length = struct.unpack("!II", header)
    payload = recv_all(conn, length) if length else b""
    if payload is None:
        return None, None
    return msg_type, payload

def peer_server():
    deadline = time.time() + 10
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", 5682))
        server.listen(8)
        server.settimeout(0.2)
        while time.time() < deadline:
            try:
                conn, _ = server.accept()
            except socket.timeout:
                continue
            with conn:
                msg_type, payload = recv_msg(conn)
                if msg_type == MSG_CHOOSE_USER and len(payload) == 12:
                    card_id, port, cost = struct.unpack("!III", payload)
                    log(f"CHOOSE_USER {card_id} {port} {cost}")
                    if cost == 4242:
                        return

def send_available(conn, card_id):
    text = f"card manuale {card_id}".encode("ascii") + b"\0"
    payload = struct.pack("!IIII", card_id, 2, 1, 5682) + text
    conn.sendall(struct.pack("!II", MSG_AVAILABLE_CARD, len(payload)) + payload)
    log(f"AVAILABLE_CARD {card_id}")

threading.Thread(target=peer_server, daemon=True).start()

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 5678))
    server.listen(1)
    conn, _ = server.accept()
    with conn:
        msg_type, payload = recv_msg(conn)
        if msg_type != MSG_HELLO:
            log(f"MESSAGGIO_INATTESO {msg_type}")
            sys.exit(1)
        port = struct.unpack("!I", payload)[0]
        log(f"HELLO {port}")
        send_available(conn, 123)
        send_available(conn, 124)
        deadline = time.time() + 10
        while time.time() < deadline:
            conn.settimeout(max(0.1, deadline - time.time()))
            try:
                msg_type, payload = recv_msg(conn)
            except socket.timeout:
                break
            if msg_type is None:
                break
            if msg_type == MSG_HELLO and len(payload) == 4:
                port = struct.unpack("!I", payload)[0]
                log(f"HELLO_MANUALE {port}")
                continue
            if msg_type == MSG_ACK_CARD and len(payload) == 4:
                card_id = struct.unpack("!I", payload)[0]
                log(f"ACK_CARD {card_id}")
                break
            log(f"MESSAGGIO {msg_type}")
PY
    PIDS+=("$!")
    sleep 1

    start_utente_on_port 5681 "$user_fifo" "$user_log"
    wait_for_count "due AVAILABLE_CARD ricevuti per random" 10 'AVAILABLE_CARD ricevuto' "$user_log" 2

    cost_count="$(grep 'AVAILABLE_CARD ricevuto' "$user_log" | sed -E 's/.*costo locale ([0-9-]+),.*/\1/' | sort -u | wc -l)"
    if [ "$cost_count" -lt 2 ]; then
        first_cost="$(grep 'AVAILABLE_CARD ricevuto' "$user_log" | sed -n '1s/.*costo locale \([0-9-]*\),.*/\1/p')"
        second_cost="$(grep 'AVAILABLE_CARD ricevuto' "$user_log" | sed -n '2s/.*costo locale \([0-9-]*\),.*/\1/p')"
        fail "costi random distinti ($first_cost, $second_cost)"
    fi
    pass "costi random distinti"

    printf 'HELLO\n' >&7
    wait_for_log "HELLO manuale inviato da terminale" 5 'HELLO_MANUALE 5681' "$fake_log"

    printf 'CHOOSE_USER 4242\n' >&7
    wait_for_log "CHOOSE_USER manuale inviato da terminale" 10 'CHOOSE_USER 124 5681 4242' "$fake_log"

    printf 'ACK_CARD 124\n' >&7
    wait_for_log "ACK_CARD manuale inviato da terminale" 5 'Invio ACK_CARD per card 124' "$user_log"
    wait_for_log "ACK_CARD manuale ricevuto dalla lavagna finta" 10 'ACK_CARD 124' "$fake_log"

    exec 7>&-
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
    wait_for_log "uscita primo utente churn" 20 "Utente 5685 rimosso dalla lavagna" "$lavagna_log"
    printf 'QUIT\n' >&8
    wait_for_log "uscita secondo utente churn" 20 "Utente 5686 rimosso dalla lavagna" "$lavagna_log"
    wait_for_log "quattro utenti dopo uscite" 15 'Utenti registrati (4)' "$lavagna_log"

    start_utente_on_port 5687 /dev/null "$WORKDIR/churn_new1.log"
    start_utente_on_port 5688 /dev/null "$WORKDIR/churn_new2.log"
    wait_for_log "rientro nuovo utente" 15 'Utenti registrati (5)' "$lavagna_log"
    wait_for_log "rientro secondo nuovo utente" 15 'Utenti registrati (6)' "$lavagna_log"

    exec 7>&-
    exec 8>&-
    cleanup
}

check_manual_card_done_command() {
    local lavagna_log="$WORKDIR/manual_done_lavagna.log"
    local u1_fifo="$WORKDIR/manual_done_u1.in"
    local u2_fifo="$WORKDIR/manual_done_u2.in"
    local winner
    local winner_log

    cleanup
    mkfifo "$u1_fifo" "$u2_fifo"
    exec 7<>"$u1_fifo"
    exec 8<>"$u2_fifo"

    start_lavagna "$lavagna_log"
    start_utente_on_port 5681 "$u1_fifo" "$WORKDIR/manual_done_u5681.log"
    start_utente_on_port 5684 "$u2_fifo" "$WORKDIR/manual_done_u5684.log"

    wait_for_log "presa in carico per CARD_DONE manuale" 15 'ACK_CARD ricevuto da' "$lavagna_log"
    winner="$(grep 'ACK_CARD ricevuto da' "$lavagna_log" | tail -1 | sed -E 's/.*da ([0-9]+) per.*/\1/')"

    if [ "$winner" = "5681" ]; then
        winner_log="$WORKDIR/manual_done_u5681.log"
        printf 'CARD_DONE 1\n' >&7
    elif [ "$winner" = "5684" ]; then
        winner_log="$WORKDIR/manual_done_u5684.log"
        printf 'CARD_DONE 1\n' >&8
    else
        fail "individuazione vincitore per CARD_DONE manuale"
    fi

    wait_for_log "CARD_DONE manuale inviato da terminale" 5 'Invio CARD_DONE per card 1' "$winner_log"
    wait_for_log "CARD_DONE manuale ricevuto dalla lavagna" 10 "CARD_DONE ricevuto da $winner per la card 1" "$lavagna_log"

    exec 7>&-
    exec 8>&-
    cleanup
}

check_unexpected_disconnect() {
    local lavagna_log="$WORKDIR/crash_lavagna.log"
    local u1_log="$WORKDIR/crash_u5681.log"
    local u2_log="$WORKDIR/crash_u5684.log"
    local u3_log="$WORKDIR/crash_u5687.log"
    local u1_pid
    local u2_pid
    local victim_pid
    local winner

    cleanup
    start_lavagna "$lavagna_log"

    "$ROOT/utente" 5681 < /dev/null > "$u1_log" 2>&1 &
    u1_pid="$!"
    PIDS+=("$u1_pid")
    "$ROOT/utente" 5684 < /dev/null > "$u2_log" 2>&1 &
    u2_pid="$!"
    PIDS+=("$u2_pid")

    wait_for_log "offerta prima card prima del crash" 15 'AVAILABLE_CARD inviato per la card 1' "$lavagna_log"
    wait_for_log "presa in carico prima del crash" 15 'ACK_CARD ricevuto da' "$lavagna_log"

    winner="$(grep 'ACK_CARD ricevuto da' "$lavagna_log" | tail -1 | sed -E 's/.*da ([0-9]+) per.*/\1/')"
    if [ "$winner" = "5681" ]; then
        victim_pid="$u1_pid"
    elif [ "$winner" = "5684" ]; then
        victim_pid="$u2_pid"
    else
        fail "individuazione utente attivo prima del crash"
    fi

    kill -KILL "$victim_pid" 2>/dev/null || fail "terminazione inattesa utente attivo"
    wait "$victim_pid" 2>/dev/null || true
    drop_pid "$victim_pid"
    wait_for_log "rilevata chiusura inattesa utente attivo" 15 "Connessione chiusa dall'utente $winner" "$lavagna_log"
    wait_for_log "utente attivo rimosso dopo crash" 15 "Utente $winner rimosso dalla lavagna" "$lavagna_log"
    wait_for_log "card tornata To Do dopo crash" 15 'Card ID: 1  User: 0' "$lavagna_log"

    start_utente_on_port 5687 /dev/null "$u3_log"
    wait_for_count "card riassegnata dopo crash" 20 'AVAILABLE_CARD inviato per la card 1' "$lavagna_log" 2
    wait_for_count "nuovo ACK dopo crash" 20 'ACK_CARD ricevuto da' "$lavagna_log" 2

    cleanup
}

check_payload_validation() {
    local lavagna_log="$WORKDIR/payload_lavagna.log"
    local valid_log="$WORKDIR/payload_valid_u5679.log"
    local valid2_log="$WORKDIR/payload_valid_u5680.log"

    cleanup
    start_lavagna "$lavagna_log"

    send_raw_protocol_message 1 8 "0000162f00000000" || fail "invio HELLO malformato"
    sleep 1
    assert_log_absent "HELLO con payload troppo lungo rifiutato" 'HELLO ricevuto dall'\''utente 5679' "$lavagna_log"

    send_raw_protocol_message 3 1025 "" || fail "invio payload oltre limite"
    sleep 1
    send_raw_protocol_message 99 0 "" || fail "invio tipo messaggio non valido"
    sleep 1

    if ! python3 - <<'PY'
import socket
import struct
import time

with socket.create_connection(("127.0.0.1", 5678), timeout=3) as sock:
    sock.sendall(struct.pack("!II", 1, 4) + struct.pack("!I", 5681))
    time.sleep(0.2)
    payload = b"100 TODO card senza terminatore"
    sock.sendall(struct.pack("!II", 3, len(payload)) + payload)
    time.sleep(0.2)
PY
    then
        fail "invio CREATE_CARD senza terminatore"
    fi
    sleep 1
    assert_log_absent "CREATE_CARD senza terminatore rifiutata" 'Card 100 creata' "$lavagna_log"

    start_utente_on_port 5679 /dev/null "$valid_log"
    wait_for_log "lavagna attiva dopo payload malformati" 10 'HELLO ricevuto dall'\''utente 5679' "$lavagna_log"
    start_utente_on_port 5680 /dev/null "$valid2_log"
    wait_for_log "secondo utente valido dopo payload malformati" 10 'HELLO ricevuto dall'\''utente 5680' "$lavagna_log"

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

    printf 'PONG_LAVAGNA\n' >&7
    wait_for_log "PONG_LAVAGNA da utente" 10 "PONG_LAVAGNA ricevuto dall'utente $u1_port" "$lavagna_log"

    printf 'SHOW_LAVAGNA\n' >&7
    wait_for_count "SHOW_LAVAGNA da utente" 10 'Lavagna 1' "$lavagna_log" 2

    printf 'SEND_USER_LIST\n' >&7
    wait_for_log "SEND_USER_LIST rifiutato sull'utente" 10 'Comando non valido per utente' "$WORKDIR/commands_u5679.log"

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
    local lavagna_pid

    cleanup
    mkfifo "$lavagna_fifo" "$u1_fifo" "$u2_fifo"
    exec 9<>"$lavagna_fifo"
    exec 7<>"$u1_fifo"
    exec 8<>"$u2_fifo"

    "$ROOT/lavagna" < "$lavagna_fifo" > "$lavagna_log" 2>&1 &
    lavagna_pid="$!"
    PIDS+=("$lavagna_pid")
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

    printf 'AVAILABLE_CARD\n' >&9
    wait_for_log "AVAILABLE_CARD da lavagna" 10 'AVAILABLE_CARD richiesto da terminale' "$lavagna_log"

    printf 'CREATE_CARD 88 TODO Card creata dalla lavagna\n' >&9
    wait_for_log "CREATE_CARD rifiutato sulla lavagna" 10 'Comando non valido per la lavagna' "$lavagna_log"

    printf 'MOVE_CARD 2 TODO\n' >&9
    wait_for_log "MOVE_CARD da lavagna" 10 'Card ID: 2' "$lavagna_log"

    printf 'SEND_USER_LIST 5684\n' >&9
    wait_for_log "SEND_USER_LIST da lavagna" 10 'Lista utenti ricevuta' "$WORKDIR/cmd_u5684.log"

    printf 'QUIT\n' >&9
    wait_for_count "QUIT rifiutato sulla lavagna" 10 'Comando non valido per la lavagna' "$lavagna_log" 2

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
    check_single_user_behavior
    check_manual_ack_and_random_seed
    check_payload_validation
    check_user_count_range_1_to_50
    check_user_churn
    check_manual_card_done_command
    check_unexpected_disconnect
    check_full_even_flow
    check_terminal_commands
    check_lavagna_commands_and_ping

    printf 'TUTTI I CONTROLLI SONO PASSATI\n'
}

main "$@"
