CFLAGS=-Wall -Wextra -pedantic -Wno-unused-parameter
LFLAGS=-lX11 -lpulse -pthread
CFG=-DSND_PULSEAUDIO

debug:
	$(CC) -g -o ssbd ssb.c $(CFLAGS) $(LFLAGS) $(CFG)

fast:
	$(CC) -o ssb ssb.c -O2 $(CFLAGS) $(LFLAGS) $(CFG)

install: fast
	cp -f ssb /usr/bin/
	chmod 755 /usr/bin/ssb

uninstall:
	rm -f /usr/bin/ssb
