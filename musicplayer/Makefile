CC=gcc
CFLAGS=-g -fPIC -I. -lev -lpthread -lasound -lmad

all: echoserver client

echoserver: stream_io.c ev_rbtree.c main.c
	$(CC) $(CFLAGS) $^ -o $@

client: stream_io.c ev_rbtree.c main2.c
	$(CC) $(CFLAGS) $^ -o $@

clean: echoserver client
	rm $^
