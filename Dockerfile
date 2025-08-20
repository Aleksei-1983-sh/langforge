
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    libpq-dev \
    postgresql-client \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Копируем код
COPY . .

# Собираем проект
RUN make

# Переменные окружения для тестов/подключения к postgres (docker-compose их перезапишет)
ENV PGUSER=testuser
ENV PGPASSWORD=testpass
ENV PGDATABASE=testdb
ENV PGHOST=postgres

# Скрипт запуска тестов (по умолчанию)
CMD ["/app/tests/run_tests.sh"]
