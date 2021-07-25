CC=gcc
CFLAGS=--std=gnu17 -Wall
LDFLAGS=-pthread

all: htcpcpd

htcpcpd: src/htcpcpd.c
	$(CC) $(CFLAGS) $(LDFLAGS) src/htcpcpd.c -o htcpcpd

install: htcpcpd
	cp htcpcpd /usr/local/bin/

clean:
	rm -f htcpcpd
