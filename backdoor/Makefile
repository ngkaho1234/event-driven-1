CC=gcc
CFLAGS=-g -fPIC -I. -I.. -lev

STREAM_IO=../stream_io.c

all: server client

server: $(STREAM_IO) main.c
	$(CC) $(CFLAGS) $^ -o $@

client: $(STREAM_IO) main2.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f server client
