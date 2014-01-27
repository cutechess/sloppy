# Unix/Linux Makefile for Sloppy

# set up compiler and options
CC = cc
CFLAGS = -O3 -pipe -mtune=generic -Wall -pedantic
LDFLAGS = -lpthread -ldl
EXECUTABLE = sloppy

DEBUGLEVEL = 1
DEFS = -DDEBUG_LEVEL=$(DEBUGLEVEL) -DUSE_THREADS

OBJS = avltree.o bench.o chess.o debug.o egbb.o eval.o hash.o main.o \
       notation.o game.o input.o makemove.o pgn.o book.o magicmoves.o \
       movegen.o perft.o search.o thread.o util.o xboard.o

.c.o:
	$(CC) -std=gnu99 -c $(CFLAGS) $< $(DEFS)

all: $(OBJS)
	$(CC) $(OBJS) -o $(EXECUTABLE) $(LDFLAGS)

clean:
	rm -f $(OBJS)

clobber: clean
	rm -f $(EXECUTABLE)

# End Makefile
