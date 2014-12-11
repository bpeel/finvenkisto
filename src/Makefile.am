SUBDIRS = rply

bin_PROGRAMS = finvenkisto

AM_CFLAGS = \
	$(EPOXY_CFLAGS) \
	$(SDL_CFLAGS) \
	$(WARNING_CFLAGS) \
	$(NULL)

finvenkisto_SOURCES = \
	fv-buffer.c \
	fv-buffer.h \
	fv-data.c \
	fv-data.h \
	fv-game.c \
	fv-game.h \
	fv-image.c \
	fv-image.h \
	fv-logic.c \
	fv-logic.h \
	fv-main.c \
	fv-map.c \
	fv-map.h \
	fv-map-painter.c \
	fv-map-painter.h \
	fv-matrix.c \
	fv-matrix.h \
	fv-shader-data.c \
	fv-shader-data.h \
	fv-transform.c \
	fv-transform.h \
	fv-util.c \
	fv-util.h \
	stb_image.h \
	$(NULL)

BUILT_SOURCES = \
	fv-map.c \
	$(NULL)

fv-map.c : fv-map.txt make-map.py
	$(AM_V_GEN)python3 $(srcdir)/make-map.py < $(srcdir)/fv-map.txt > $@

finvenkisto_LDADD = \
	$(EPOXY_LIBS) \
	$(SDL_LIBS) \
	rply/librply.a \
	$(NULL)

EXTRA_DIST = \
	fv-map.txt \
	make-map.py \
	$(NULL)
