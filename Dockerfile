

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    libpq-dev \
    postgresql-client \
    curl \
    ca-certificates \
    psmisc \
    && rm -rf /var/lib/apt/lists/*

# Собираем в отдельной рабочей директории, чтобы потом не перезаписывать /app томом
WORKDIR /build

# Копируем весь проект в /build (в образе)
COPY . .

# Собираем проект (make должен положить результат в ./bin/englearn)
RUN make

RUN mkdir -p /build/log

# Переменные окружения для тестов/подключения к postgres (docker-compose перезапишет при необходимости)
ENV PGUSER=testuser
ENV PGPASSWORD=testpass
ENV PGDATABASE=testdb
ENV PGHOST=postgres

# По умолчанию запускаем бинарник (compose может переопределить команду)
CMD ["/usr/local/bin/englearn"]

