SUBDIRS = data rply

bin_PROGRAMS = finvenkisto

AM_CFLAGS = \
	$(VULKAN_CFLAGS) \
	$(SDL_CFLAGS) \
	$(WARNING_CFLAGS) \
	$(NULL)

sources = \
	$(NULL)

BUILT_SOURCES = \
	fv-map.c \
	$(NULL)

fv-map.c : fv-map.png make-map.py
	$(AM_V_GEN)python3 $(srcdir)/make-map.py $(srcdir)/fv-map.png > $@

finvenkisto_SOURCES = \
	fv-allocate-store.c \
	fv-allocate-store.h \
	fv-buffer.c \
	fv-buffer.h \
	fv-data.c \
	fv-data.h \
	fv-ease.c \
	fv-ease.h \
	fv-error-message.c \
	fv-error-message.h \
	fv-game.c \
	fv-game.h \
	fv-hud.c \
	fv-hud.h \
	fv-image-data.c \
	fv-image-data.h \
	fv-list.c \
	fv-list.h \
	fv-logic.c \
	fv-logic.h \
	fv-main.c \
	fv-map.c \
	fv-map.h \
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
	fv-pipeline-data.c \
	fv-pipeline-data.h \
	fv-shout-painter.c \
	fv-shout-painter.h \
	fv-transform.c \
	fv-transform.h \
	fv-util.c \
	fv-util.h \
	fv-vertex.h \
	fv-vk.c \
	fv-vk.h \
	fv-vk-core-funcs.h \
	fv-vk-data.h \
	fv-vk-device-funcs.h \
	fv-vk-instance-funcs.h \
	stb_image.h \
	$(NULL)
finvenkisto_LDADD = \
	$(SDL_LIBS) \
	-ldl \
	rply/librply.a \
	$(NULL)

EXTRA_DIST = \
	fv-map.png \
	make-map.py \
	$(NULL)
