# syntax=docker/dockerfile:1

FROM debian:bookworm-slim AS build
ARG REPO_URL=https://github.com/suinevere/saturn-mojozork.git
ARG REPO_REF=main
RUN apt-get update \
 && apt-get install -y --no-install-recommends git ca-certificates gcc libc6-dev libsqlite3-dev \
 && rm -rf /var/lib/apt/lists/*
ADD https://api.github.com/repos/suinevere/saturn-mojozork/commits/${REPO_REF} /tmp/commit.json
RUN git clone --depth 1 --branch "${REPO_REF}" "${REPO_URL}" /src
WORKDIR /src/saturn
RUN gcc -O2 -o /multizorkd multizorkd.c -lsqlite3

FROM debian:bookworm-slim AS runtime
RUN apt-get update \
 && apt-get install -y --no-install-recommends libsqlite3-0 \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --uid 1000 --user-group --home-dir /data --shell /usr/sbin/nologin multizork \
 && mkdir -p /data \
 && chown multizork:multizork /data
COPY --from=build /multizorkd /app/multizorkd
COPY --from=build /src/saturn/cd/data/Z3/ZORK1.Z3 /app/ZORK1.Z3
WORKDIR /data
VOLUME ["/data"]
USER multizork
EXPOSE 2323
CMD ["/app/multizorkd", "--port", "2323", "/app/ZORK1.Z3"]
