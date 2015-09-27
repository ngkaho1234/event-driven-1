CC=gcc
CFLAGS=-g -fPIC -I. -I.. -lev -laio

all: echoserver client

echoserver: ../stream_io.c main.c
	$(CC) $(CFLAGS) $^ -o $@

client: ../stream_io.c main2.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f echoserver client
