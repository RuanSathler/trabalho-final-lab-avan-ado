
CC = gcc
CFLAGS = -Wall

all: battleserver battleclient

battleserver: server/battleserver.c common/protocol.h
	$(CC) $(CFLAGS) -o server/battleserver server/battleserver.c

battleclient: client/battleclient.c common/protocol.h
	$(CC) $(CFLAGS) -o client/battleclient client/battleclient.c

clean:
	rm -f server/battleserver client/battleclient
