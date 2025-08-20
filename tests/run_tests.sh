#!/usr/bin/env bash
set -e

: "${PGHOST:=postgres}"
: "${PGUSER:=testuser}"
: "${PGPASSWORD:=testpass}"
: "${PGDATABASE:=testdb}"

export PGPASSWORD="$PGPASSWORD"

echo "Waiting for Postgres at ${PGHOST}..."
for i in $(seq 1 30); do
  if pg_isready -h "$PGHOST" -U "$PGUSER" -d "$PGDATABASE" >/dev/null 2>&1; then
    echo "Postgres ready"
    break
  fi
  echo -n "."
  sleep 1
done

# Применяем схему (если контейнер db не сделал этого автоматически)
if [ -f /app/src/db/schema.sql ]; then
  echo "Applying schema.sql..."
  psql -h "$PGHOST" -U "$PGUSER" -d "$PGDATABASE" -f /app/src/db/schema.sql
fi

# Минимальная проверка: выполнить простую команду
echo "Running simple DB check..."
psql -h "$PGHOST" -U "$PGUSER" -d "$PGDATABASE" -c "SELECT 1;" || { echo "DB query failed"; exit 2; }

# (Опционально) Запустить бинарник, если он существует, и проверить доступность HTTP-эндоинта /health
if [ -x /usr/local/bin/englearn ]; then
  echo "Starting binary in background..."
  /usr/local/bin/englearn &>/tmp/englearn.log &
  APP_PID=$!
  sleep 2
  # Если у тебя есть health endpoint, можно закомментировать curl строку и раскомментировать
  # curl -fsS http://localhost:8080/health || echo "Health check failed (ok for now)"
  kill $APP_PID || true
fi

echo "Tests finished OK"
exit 0
