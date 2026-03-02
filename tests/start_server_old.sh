#!/usr/bin/env bash
# ./tests/start_server.sh
#— запускает backend с проверками Postgres и mock LLM (ollama mock)
# поведение: проверяет зависимости, применяет схему, запускает бинарник и следит за ним.
set -euo pipefail


: "${SESSION_MAX_AGE:=2592000}"

: "${PGHOST:=postgres}"
: "${PGUSER:=testuser}"
: "${PGPASSWORD:=testpass}"
: "${PGDATABASE:=testdb}"

: "${OLLAMA_HOST:=ollama}"
: "${OLLAMA_PORT:=11434}"
: "${SERVER_PORT:=1234}"

SCHEMA_PATH="/app/src/db/schema.sql"
LOGFILE="/tmp/englearn.log"
BIN_CANDIDATES=("/app/bin/englearn")
WAIT_ATTEMPTS=30
WAIT_SLEEP=1

export PGPASSWORD="$PGPASSWORD"

log() { printf '%s %s\n' "$(date -u +"%Y-%m-%dT%H:%M:%SZ")" "$*"; }
err() { log "ERROR: $*"; }

wait_for_postgres() {
  log "Waiting for Postgres at ${PGHOST}..."
  for i in $(seq 1 $WAIT_ATTEMPTS); do
    if pg_isready -h "$PGHOST" -U "$PGUSER" -d "$PGDATABASE" >/dev/null 2>&1; then
      log "Postgres ready"
      return 0
    fi
    sleep $WAIT_SLEEP
  done
  err "Postgres not ready"
  return 1
}

apply_schema_if_present() {
  if [ -f "$SCHEMA_PATH" ]; then
    log "Applying schema from $SCHEMA_PATH..."
    psql -h "$PGHOST" -U "$PGUSER" -d "$PGDATABASE" -f "$SCHEMA_PATH"
    log "Schema applied"
  fi
}

simple_db_check() {
  log "Running simple DB check..."
  psql -h "$PGHOST" -U "$PGUSER" -d "$PGDATABASE" -c "SELECT 1;" >/dev/null 2>&1 || return 1
  log "DB check OK"
  return 0
}

wait_for_ollama() {
  local url="http://${OLLAMA_HOST}:${OLLAMA_PORT}/ping"
  log "Waiting for mock LLM at ${url} ..."
  for i in $(seq 1 $WAIT_ATTEMPTS); do
    if curl -fsS --connect-timeout 1 "$url" >/dev/null 2>&1; then
      log "Mock LLM responded"
      return 0
    fi
    sleep $WAIT_SLEEP
  done
  err "Mock LLM not responding"
#  return 1
}

is_port_listening() {
  if command -v ss >/dev/null 2>&1; then
    ss -tln | awk '{print $4}' | grep -q -E "[:.]${SERVER_PORT}\$" && return 0 || return 1
  elif command -v netstat >/dev/null 2>&1; then
    netstat -tln | awk '{print $4}' | grep -q -E "[:.]${SERVER_PORT}\$" && return 0 || return 1
  else
    log "Warning: no ss/netstat, skipping port check (assuming server is up if process alive)"
    sleep 2
    kill -0 "$child_pid" 2>/dev/null && return 0 || return 1
  fi
}

find_binary() {
  for b in "${BIN_CANDIDATES[@]}"; do
    if [ -x "$b" ]; then
      printf '%s' "$b"
      return 0
    fi
  done
  return 1
}

log "=== start_server.sh ==="

wait_for_postgres || exit 2
apply_schema_if_present
simple_db_check || exit 2
wait_for_ollama || exit 3

BIN_PATH="$(find_binary || true)"
if [ -z "$BIN_PATH" ]; then
  err "Binary not found"
  exit 4
fi

log "Starting binary: $BIN_PATH"
mkdir -p "$(dirname "$LOGFILE")"
"$BIN_PATH" &
child_pid=$!
log "PID $child_pid, waiting for port $SERVER_PORT..."

for i in $(seq 1 $WAIT_ATTEMPTS); do
  if ! kill -0 "$child_pid" 2>/dev/null; then
    err "Process died"
    exit 5
  fi
  if is_port_listening; then
    log "Server is listening"
    break
  fi
  sleep $WAIT_SLEEP
done

if ! is_port_listening; then
  err "Port not listening in time"
  kill -9 "$child_pid" 2>/dev/null || true
  exit 6
fi

log "Server seems up. Waiting for process to finish..."
wait "$child_pid"
exit 0
