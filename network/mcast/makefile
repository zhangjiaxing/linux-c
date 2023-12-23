CC=gcc
CFLAGS= -O2

all: sender receiver

sender: mcast_sender.c
	$(CC) $(CFLAGS) -o $@ $^

receiver: mcast_receiver.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf sender receiver

