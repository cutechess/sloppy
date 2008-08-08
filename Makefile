# Unix/Linux Makefile for Sloppy

# set up compiler and options
CC = gcc
CFLAGS = -O3 -pipe -mtune=generic
LDFLAGS = -lpthread -ldl
EXECUTABLE = sloppy

NAME = Sloppy
DEBUGLEVEL = 1
DEFS = -DAPP_NAME='"$(NAME)"' -DDEBUG_LEVEL=$(DEBUGLEVEL)

OBJS = avltree.o bench.o chess.o debug.o egbb.o eval.o hash.o main.o \
       notation.o game.o input.o makemove.o pgn.o book.o magicmoves.o \
       movegen.o perft.o search.o util.o xboard.o

.c.o:
	$(CC) -std=gnu99 -c -Wall -pedantic $(CFLAGS) $< $(DEFS)

all: $(OBJS)
	$(CC) $(OBJS) -o $(EXECUTABLE) $(LDFLAGS)

clean:
	rm -f $(OBJS)

clobber: clean
	rm -f $(EXECUTABLE)

# End Makefile
