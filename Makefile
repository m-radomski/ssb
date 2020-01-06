CFLAGS=-Wall -Wextra -pedantic
LFLAGS=-lX11

debug:
	$(CC) -g -o ssb ssb.c $(CFLAGS) $(LFLAGS)

fast:
	$(CC) -o ssb ssb.c -O2 $(CFLAGS) $(LFLAGS)

install: fast
	cp -f ssb /usr/bin/
	chmod 755 /usr/bin/ssb

uninstall:
	rm -f /usr/bin/ssb
