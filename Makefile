CC=gcc
CFLAGS=--std=gnu17 -Wall

ifeq ($(DEBUG),1)
	CFLAGS += -g -DDEBUG
else
	CFLAGS += -O3
endif

all: httpd

httpd: src/httpd.c src/httpd.h src/config.c src/config.h
	$(CC) $(CFLAGS) src/httpd.c src/config.c -o httpd

install: httpd
	cp httpd /usr/local/bin/

clean:
	rm -f httpd
