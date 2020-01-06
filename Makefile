CFLAGS=-Wall -Wextra
LFLAGS=-lX11

fast:
	$(CC) -o ssb ssb.c -O2 $(CFLAGS) $(LFLAGS)
debug:
	$(CC) -g -o ssb ssb.c $(CFLAGS) $(LFLAGS)
install: fast
	cp -f ssb /usr/bin/
	chmod 755 /usr/bin/ssb

uninstall:
	rm -f /usr/bin/ssb
