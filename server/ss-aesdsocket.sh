#!/bin/sh

case "$1" in
  start)
    printf "Starting aesdsocket...\n"
    start-stop-daemon -S -b -n aesdsocket -a /usr/bin/aesdsocket -- -d
    ;;
  stop)
    printf "Stopping aesdsocket...\n"
    start-stop-daemon -K -n aesdsocket
    ;;
  *)
    printf "Usage: $0 {start|stop}\n"
    exit 1
esac
exit 0

