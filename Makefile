CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2
WARNINGS = -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

.PHONY: all test install uninstall clean
all: build/neotimer

build:
	mkdir -p $@

build/neotimer: src/neotimer.c src/timer.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) src/neotimer.c -o $@

build/test_timer: tests/test_timer.c src/timer.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) tests/test_timer.c -o $@

test: build/neotimer build/test_timer
	./build/test_timer
	python3 tests/test_cli.py ./build/neotimer

install: build/neotimer
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 755 build/neotimer "$(DESTDIR)$(BINDIR)/neotimer"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/neotimer"

clean:
	rm -rf build
