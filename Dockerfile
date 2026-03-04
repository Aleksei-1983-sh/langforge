# ==============================
# FRONTEND BUILD STAGE
# ==============================
FROM node:20-alpine AS frontend-build

WORKDIR /frontend
COPY www/package*.json ./
RUN npm ci

COPY www ./
RUN npm run build

# ==============================
# BACKEND BUILD STAGE
# ==============================
FROM ubuntu:24.04 AS backend-build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    libpq-dev \
    postgresql-client \
    curl \
    ca-certificates \
    psmisc \
    python3 \
    strace \
    vim \
    net-tools \
    tree\
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .
RUN make

# ==============================
# FINAL RUNTIME IMAGE
# ==============================
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    libpq5 \
    postgresql-client \
    ca-certificates \
    tree\
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Копируем backend
COPY --from=backend-build /build/bin/englearn /app/bin/englearn
# Копируем тесты/скрипты из backend-build в финальный образ
COPY --from=backend-build /build/tests /app/tests

# Убедимся, что скрипт исполняемый
RUN chmod +x /app/tests/start_server_old.sh

# Копируем уже собранный фронтенд
COPY --from=frontend-build /frontend/dist /app/www/

ENV PGUSER=testuser
ENV PGPASSWORD=testpass
ENV PGDATABASE=testdb
ENV PGHOST=postgres

CMD ["/app/bin/englearn"]
