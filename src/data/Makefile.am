HUD_SVGS = \
	hud/fina.svg \
	hud/crocodile.svg \
	hud/down.svg \
	hud/left.svg \
	hud/player_select.svg \
	hud/push.svg \
	hud/right.svg \
	hud/shout.svg \
	hud/star.svg \
	hud/title.svg \
	hud/up.svg \
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

SHADERS = \
	fv-hud-vertex.glsl \
	fv-special-fragment.glsl \
	fv-special-vertex.glsl \
	fv-texture-fragment.glsl \
	fv-texture-vertex.glsl \
	fv-person-fragment.glsl \
	fv-person-vertex.glsl \
	$(NULL)

MODELS = \
	person.ply \
	table.ply \
	toilet.ply \
	teaset.ply \
	chair.ply \
	bed.ply \
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

imagesdir = $(datadir)/finvenkisto
dist_images_DATA = \
	ARTCREDITS.txt \
	$(SHADERS) \
	$(MODELS) \
	$(OTHER_PNGS) \
	bambo1.png \
	bambo2.png \
	bambo3.png \
	finvenkisto.png \
	gufujestro.png \
	map-texture.png \
	pyjamas.png \
	hud.png \
	toiletguy.png \
	$(NULL)

EXTRA_DIST = \
	$(HUD_SVGS) \
	$(HUD_PNGS) \
	$(OTHER_SVGS) \
	$(OTHER_PNGS) \
	hud.png \
	hud-layout.h \
	make-atlas.py \
	make-digits.py \
	make-map-texture.py \
	hud/digits-stamp \
	hud-stamp \
	$(HUD_DIGITS) \
	$(MODELS:.ply=.blend) \
	$(BLOCK_IMAGES) \
	$(NULL)

.svg.png :
	$(AM_V_GEN)mkdir -p `dirname $@`
	$(AM_V_at)inkscape -e $@ $<

hud.png hud-layout.h : hud-stamp

hud-stamp : $(HUD_PNGS) make-atlas.py
	$(AM_V_GEN)mkdir -p $(builddir)/hud
	$(AM_V_at)python3 $(srcdir)/make-atlas.py \
	hud.png hud-layout.h $(filter %.png,$^)
	$(AM_V_at)touch $@

DISTCLEANFILES = \
	$(HUD_PNGS) \
	hud-layout.h \
	hud.png \
	hud/digits-stamp \
	hud-stamp \
	$(NULL)
