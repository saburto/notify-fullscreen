CC       ?= cc
PKGS     := cairo x11
CFLAGS   ?= -O2
CFLAGS   += -std=gnu11 -Wall -Wextra -Wpedantic -Isrc
CPPFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS   += $(shell pkg-config --libs $(PKGS))

PREFIX   ?= $(HOME)/.local
BINDIR   ?= $(PREFIX)/bin
BIN      := notify-fullscreen

SRC := src/main.c src/app.c src/text.c src/layout.c src/preview.c \
       src/tmux.c src/util.c src/backend/x11.c
HDR := $(wildcard src/*.h src/backend/*.h)

all: $(BIN)

$(BIN): $(SRC) $(HDR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) $(LDLIBS)

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

# Render the overlay to a PNG instead of showing it (needs a DISPLAY for sizing).
preview: $(BIN)
	./$(BIN) --preview-png preview.png \
		"Deployment of api-gateway v2.4.1 failed on prod-01: health check timeout"

clean:
	rm -f $(BIN) preview.png

# CMake equivalent (used by the Windows/macOS builds later):
#   cmake -S . -B build && cmake --build build
cmake:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build

.PHONY: all install uninstall preview clean cmake