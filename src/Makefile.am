SUBDIRS = data rply

bin_PROGRAMS =

if IS_EMSCRIPTEN
bin_PROGRAMS += finvenkisto.html
else
bin_PROGRAMS += finvenkisto
endif

AM_CFLAGS = \
	$(GL_CFLAGS) \
	$(SDL_CFLAGS) \
	$(WARNING_CFLAGS) \
	$(NULL)

sources = \
	fv-array-object.c \
	fv-array-object.h \
	fv-buffer.c \
	fv-buffer.h \
	fv-data.c \
	fv-data.h \
	fv-ease.c \
	fv-ease.h \
	fv-error-message.h \
	fv-game.c \
	fv-game.h \
	fv-gl.c \
	fv-gl.h \
	fv-gl-funcs.h \
	fv-hud.c \
	fv-hud.h \
	fv-input.c \
	fv-input.h \
	fv-image-data.h \
	fv-logic.c \
	fv-logic.h \
	fv-main.c \
	fv-map.c \
	fv-map.h \
	fv-map-buffer.c \
	fv-map-buffer.h \
	fv-map-painter.c \
	fv-map-painter.h \
	fv-matrix.c \
	fv-matrix.h \
	fv-model.c \
	fv-model.h \
	fv-paint-state.h \
	fv-person.c \
	fv-person.h \
	fv-person-painter.c \
	fv-person-painter.h \
	fv-shader-data.c \
	fv-shader-data.h \
	fv-shout-painter.c \
	fv-shout-painter.h \
	fv-transform.c \
	fv-transform.h \
	fv-util.c \
	fv-util.h \
	stb_image.h \
	$(NULL)

BUILT_SOURCES = \
	fv-map.c \
	$(NULL)

fv-map.c : fv-map.ppm make-map.py
	$(AM_V_GEN)python3 $(srcdir)/make-map.py $(srcdir)/fv-map.ppm > $@

ldadd = \
	$(SDL_LIBS) \
	rply/librply.a \
	$(NULL)

finvenkisto_SOURCES = \
	$(sources) \
	fv-error-message-native.c \
	fv-image-data-native.c \
	$(NULL)
finvenkisto_LDADD = $(ldadd)

finvenkisto_html_SOURCES = \
	$(sources) \
	fv-error-message-emscripten.c \
	fv-image-data-emscripten.c \
	$(NULL)
finvenkisto_html_LDFLAGS = \
	--pre-js $(srcdir)/configure-emscripten.js \
	--pre-js $(builddir)/data/finvenkisto-data.js \
	--shell-file $(srcdir)/finvenkisto-shell.html \
	-sFORCE_FILESYSTEM \
	-s'EXPORTED_RUNTIME_METHODS=["UTF8ToString"]' \
	$(NULL)
finvenkisto_html_LDADD = \
	$(ldadd) \
	$(NULL)

EXTRA_DIST = \
	configure-emscripten.js \
	fv-map.ppm \
	make-map.py \
	$(NULL)
