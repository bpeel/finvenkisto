HUD_SVGS = \
	hud/ludanto.svg \
	hud/ludantoj.svg \
	hud/num_players_1.svg \
	hud/num_players_2.svg \
	hud/num_players_3.svg \
	hud/num_players_4.svg \
	hud/fina.svg \
	hud/crocodile.svg \
	hud/press_a_button.svg \
	hud/star.svg \
	hud/title.svg \
	hud/venko.svg \
	$(NULL)

HUD_DIGITS = \
	hud/digit0.png \
	hud/digit1.png \
	hud/digit2.png \
	hud/digit3.png \
	hud/digit4.png \
	hud/digit5.png \
	hud/digit6.png \
	hud/digit7.png \
	hud/digit8.png \
	hud/digit9.png \
	$(NULL)

$(HUD_DIGITS) : hud/digits-stamp

hud/digits-stamp : make-digits.py
	$(AM_V_GEN)mkdir -p $(builddir)/hud
	$(AM_V_at)python3 $(srcdir)/make-digits.py
	$(AM_V_at)touch $@

HUD_PNGS = \
	$(HUD_SVGS:.svg=.png) \
	$(HUD_DIGITS) \
	$(NULL)

OTHER_SVGS = \
	nekrokodilu.svg \
	$(NULL)

OTHER_PNGS = $(OTHER_SVGS:.svg=.png)

SPIRV = \
	shaders/fv-color-fragment.spirv \
	shaders/fv-texture-fragment.spirv \
	shaders/fv-lighting-texture-fragment.spirv \
	shaders/fv-person-fragment.spirv \
	shaders/fv-map-vertex.spirv \
	shaders/fv-hud-vertex.spirv \
	shaders/fv-special-color-vertex.spirv \
	shaders/fv-special-texture-vertex.spirv \
	shaders/fv-person-vertex.spirv \
	shaders/fv-texture-vertex.spirv \
	$(NULL)

MODELS = \
	person.ply \
	table.ply \
	toilet.ply \
	teaset.ply \
	chair.ply \
	bed.ply \
	barrel.ply \
	$(NULL)

# The order here must match the order for the image numbers used in
# the map
BLOCK_IMAGES = \
	walltop.png \
	bathroom-floor.png \
	brick-flooring.png \
	grass.png \
	brick-wall.png \
	inner-wall.png \
	bathroom-wall.png \
	bathroom-wall-note.png \
	room-floor.png \
	wood.png \
	table-side.png \
	welcome-poster1.png \
	welcome-poster2.png \
	sleeping-bag1.png \
	sleeping-bag2.png \
	chalkboard1.png \
	chalkboard2.png \
	paving.png \
	$(NULL)

map-texture.png : make-map-texture.py $(BLOCK_IMAGES)
	$(AM_V_GEN)python3 $(srcdir)/make-map-texture.py $@ \
	$(BLOCK_IMAGES:%=$(srcdir)/%)

NO_MIPMAP_IMAGES = \
	hud.png \
	$(NULL)

IMAGES = \
	$(OTHER_PNGS) \
	bambo1.png \
	bambo2.png \
	bambo3.png \
	finvenkisto.png \
	gufujestro.png \
	map-texture.png \
	pyjamas.png \
	toiletguy.png \
	$(NULL)

MIPMAP_IMAGES = $(IMAGES:.png=.mipng)

ALL_IMAGES = \
	$(MIPMAP_IMAGES) \
	$(NO_MIPMAP_IMAGES) \
	$(NULL)

spirvdir = $(datadir)/finvenkisto/shaders
dist_spirv_DATA = \
	$(SPIRV) \
	$(NULL)

NON_IMAGES = \
	ARTCREDITS.txt \
	$(MODELS) \
	$(NULL)

imagesdir = $(datadir)/finvenkisto
dist_images_DATA = \
	$(ALL_IMAGES) \
	$(NON_IMAGES) \
	$(NULL)

EXTRA_DIST = \
	$(MIPMAP_IMAGES) \
	$(HUD_SVGS) \
	$(HUD_PNGS) \
	$(OTHER_SVGS) \
	$(OTHER_PNGS) \
	fv-image-data-enum.h \
	fv-image-data-files.h \
	fv-image-data-stamp \
	hud.png \
	hud-layout.h \
	make-atlas.py \
	make-digits.py \
	make-map-texture.py \
	make-miping.py \
	hud/digits-stamp \
	hud-stamp \
	map-texture.png \
	$(HUD_DIGITS) \
	$(MODELS:.ply=.blend) \
	$(BLOCK_IMAGES) \
	$(SPIRV) \
	$(SPIRV:.spirv=.glsl) \
	$(NULL)

.svg.png :
	$(AM_V_GEN)mkdir -p `dirname $@`
	$(AM_V_at)inkscape -e $@ $<

.png.mipng : make-miping.py
	$(AM_V_GEN)$(srcdir)/make-miping.py $< $@

hud.png hud-layout.h : hud-stamp

hud-stamp : $(HUD_PNGS) make-atlas.py
	$(AM_V_GEN)mkdir -p $(builddir)/hud
	$(AM_V_at)python3 $(srcdir)/make-atlas.py \
	hud.png hud-layout.h $(filter %.png,$^)
	$(AM_V_at)touch $@

fv-image-data-stamp : $(ALL_IMAGES)
	$(AM_V_GEN)for x in $(ALL_IMAGES); do \
	echo "$$x" | \
	sed -e 's/\..*$$/,/' -e 's/^/fv_image_data_/' -e 's/-/_/g' \
	| tr '[a-z]' '[A-Z]'; \
	done > fv-image-data-enum.h
	$(AM_V_at)for x in $(ALL_IMAGES); \
	do echo "\"$$x\"," ; \
	done > fv-image-data-files.h
	$(AM_V_at)touch $@

DISTCLEANFILES = \
	$(HUD_PNGS) \
	$(MIPMAP_IMAGES) \
	hud-layout.h \
	fv-image-data-enum.h \
	fv-image-data-files.h \
	fv-image-data-stamp \
	hud.png \
	hud/digits-stamp \
	hud-stamp \
	$(NULL)

EXTRA_TARGETS = fv-image-data-stamp

all-local : $(EXTRA_TARGETS)
