CC=gcc
CFLAGS=--std=gnu17 -Wall
LDFLAGS=-pthread

ifeq ($(DEBUG),1)
	CFLAGS += -g -DDEBUG
else
	CFLAGS += -O3
endif

all: htcpcpd

htcpcpd: src/htcpcpd.c
	$(CC) $(CFLAGS) $(LDFLAGS) src/htcpcpd.c -o htcpcpd

install: htcpcpd
	cp htcpcpd /usr/local/bin/

clean:
	rm -f htcpcpd
