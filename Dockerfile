FROM gcc:14-bookworm AS builder

WORKDIR /app
COPY server.c .

RUN gcc -O2 -Wall -Wextra server.c -o server

FROM debian:bookworm-slim

WORKDIR /app
COPY --from=builder /app/server /app/server
COPY login.html submit.html track.html /app/

ENV PORT=10000
EXPOSE 10000

CMD ["/app/server"]
