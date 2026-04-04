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
COPY backend/ ./
RUN make -C /build

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

COPY --from=backend-build /build/bin/englearn /app/bin/englearn
COPY --from=backend-build /build/tests /app/tests
COPY --from=backend-build /build/src /app/src

RUN chmod +x /app/tests/start_server_old.sh

ENV PGUSER=testuser
ENV PGPASSWORD=testpass
ENV PGDATABASE=testdb
ENV PGHOST=postgres

CMD ["/app/bin/englearn"]
