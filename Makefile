# tekx -- a math-first document language and typesetter.
#
#   make            build ./tekx
#   make test       run unit and golden tests
#   make debug      build ./tekx-debug with AddressSanitizer and UBSan
#   make install    install into $(PREFIX) (default /usr/local)

PREFIX  ?= /usr/local
CC      ?= cc
CFLAGS  ?= -O2
WARN     = -std=c17 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
           -Wmissing-prototypes -Wno-unused-parameter -Werror
DEFS     = -D_POSIX_C_SOURCE=200809L
LDLIBS   = -lm

SRC  = main.c util.c pp.c parse.c tree.c eqn.c eqnlayout.c eqnsem.c set.c hyph.c \
       pdf.c font.c cff.c ttf.c otl.c mathtab.c deflate.c md5.c assets.c
OBJ  = $(SRC:%.c=obj/%.o)
DOBJ = $(SRC:%.c=obj-debug/%.o)
HDR  = src/tekx.h src/font.h src/tree.h src/eqn.h

ASSETS = assets/fonts/STIXTwoText-Regular.otf assets/fonts/STIXTwoText-Italic.otf \
         assets/fonts/STIXTwoText-Bold.otf assets/fonts/STIXTwoText-BoldItalic.otf \
         assets/fonts/STIXTwoMath-Regular.otf assets/fonts/LibertinusMono-Regular.otf \
         assets/hyphenation/hyph-en-us.pat.txt assets/hyphenation/hyph-en-us.hyp.txt

all: tekx

tekx: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

obj/%.o: src/%.c $(HDR) | obj
	$(CC) $(WARN) $(DEFS) $(CFLAGS) -c -o $@ $<

obj obj-debug:
	mkdir -p $@

src/assets.c: tools/bin2c $(ASSETS)
	tools/bin2c $(ASSETS) > $@

tools/bin2c: tools/bin2c.c
	$(CC) $(WARN) $(DEFS) -O2 -o $@ tools/bin2c.c

debug: tekx-debug

tekx-debug: $(DOBJ)
	$(CC) -g -fsanitize=address,undefined -o $@ $(DOBJ) $(LDLIBS)

obj-debug/%.o: src/%.c $(HDR) | obj-debug
	$(CC) $(WARN) $(DEFS) -U_FORTIFY_SOURCE -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -c -o $@ $<

test: tekx tekx-debug
	@sh test/run.sh

install: tekx
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/man/man1
	cp tekx $(DESTDIR)$(PREFIX)/bin/tekx
	cp doc/tekx.1 $(DESTDIR)$(PREFIX)/share/man/man1/tekx.1

clean:
	rm -rf obj obj-debug tekx tekx-debug src/assets.c tools/bin2c test/out

.PHONY: all debug test install clean
