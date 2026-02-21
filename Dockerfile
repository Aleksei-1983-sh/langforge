# Dockerfile
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    libpq-dev \
    postgresql-client \
    curl \
    ca-certificates \
    psmisc \
    python3 \
    && rm -rf /var/lib/apt/lists/*

# Собираем в отдельной рабочей директории, чтобы потом не перезаписывать /app томом
WORKDIR /build

# Копируем весь проект в /build (в образе)
COPY . .

# Собираем проект (make должен положить результат в ./bin/englearn)
RUN make

# Создаём /app/bin и ставим туда бинарник из результата сборки
RUN mkdir -p /app/bin \
    && install -m 0755 /build/bin/englearn /app/bin/englearn || true \
    && install -m 0755 /build/bin/englearn /usr/local/bin/englearn || true

# Лог-папка (при необходимости)
RUN mkdir -p /build/log

# Чистим исходники сборки (оставляем артефакт)
RUN rm -rf /build

# Переменные окружения по умолчанию (docker-compose может их перезаписать)
ENV PGUSER=testuser
ENV PGPASSWORD=testpass
ENV PGDATABASE=testdb
ENV PGHOST=postgres

# По умолчанию запускаем бинарник (compose может переопределить command)
CMD ["/usr/local/bin/englearn"]

