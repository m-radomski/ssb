CFLAGS=-Wall -Wextra -pedantic -Wno-unused-parameter
LFLAGS=-lX11 -lpulse -pthread

debug:
	$(CC) -g -o ssbd ssb.c $(CFLAGS) $(LFLAGS)

fast:
	$(CC) -o ssb ssb.c -O2 $(CFLAGS) $(LFLAGS)

install: fast
	cp -f ssb /usr/bin/
	chmod 755 /usr/bin/ssb

uninstall:
	rm -f /usr/bin/ssb
