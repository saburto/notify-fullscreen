CC      ?= cc
PKGS    := cairo x11
CFLAGS  ?= -O2
CFLAGS  += -std=gnu11 -Wall -Wextra -Wpedantic
CPPFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS))

PREFIX  ?= $(HOME)/.local
BINDIR  ?= $(PREFIX)/bin
BIN     := notify-fullscreen

all: $(BIN)

$(BIN): notify-fullscreen.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

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

.PHONY: all install uninstall preview clean