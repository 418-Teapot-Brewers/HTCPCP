CC=gcc
CFLAGS=--std=gnu17 -Wall -fstack-protector-all
LDFLAGS=-pthread

ifeq ($(DEBUG),1)
	CFLAGS += -g -DDEBUG
else
	CFLAGS += -O3
endif

all: htcpcpd

C_FILES := $(wildcard src/*.c)
H_FILES := $(wildcard src/*.h)
O_FILES := $(C_FILES:.c=.o)

htcpcpd: $(O_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) $(O_FILES) -o htcpcpd

%.o: %.c %.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ -c $<

install: htcpcpd
	cp htcpcpd /usr/local/bin/

clean:
	rm -f htcpcpd
