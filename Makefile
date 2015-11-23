CC=gcc
CFLAGS= -std=c99 -g -Wall -pedantic

all: clean bsh

clean: 
	$(RM) bsh

bsh: /c/cs323/Hwk5/getLine.o /c/cs323/Hwk5/parse.o /c/cs323/Hwk5/mainBsh.o process.o
	$(CC) $(CFLAGS) -o $@ $^

process.o: process.c process.h
	$(CC) $(CFLAGS) -c process.c

