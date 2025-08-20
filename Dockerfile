

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    libpq-dev \
    postgresql-client \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Собираем в отдельной рабочей директории, чтобы потом не перезаписывать /app томом
WORKDIR /build

# Копируем весь проект в /build (в образе)
COPY . .

# Собираем проект (make должен положить результат в ./bin/englearn)
RUN make

# Устанавливаем бинарник в /usr/local/bin — место вне /app, которое не перезаписывается при монтировании
RUN install -m 0755 /build/bin/englearn /usr/local/bin/englearn || \
    (mkdir -p /usr/local/bin && cp /build/bin/englearn /usr/local/bin/englearn && chmod 0755 /usr/local/bin/englearn)

# Убираем временную папку сборки, чтобы образ был чище
RUN rm -rf /build

# Переменные окружения для тестов/подключения к postgres (docker-compose перезапишет при необходимости)
ENV PGUSER=testuser
ENV PGPASSWORD=testpass
ENV PGDATABASE=testdb
ENV PGHOST=postgres

# По умолчанию запускаем бинарник (compose может переопределить команду)
CMD ["/usr/local/bin/englearn"]

