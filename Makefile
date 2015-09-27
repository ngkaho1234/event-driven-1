CC=gcc
AR=ar
CFLAGS=-g -fPIC -I. -lev

all: make_library make_backdoor

make_library: stream_io.c stream_io.h
	$(CC) $(CFLAGS) stream_io.c -c -o stream_io.o
	$(AR) rcs libstream_io.a stream_io.o

make_backdoor:
	$(MAKE) -Cbackdoor
