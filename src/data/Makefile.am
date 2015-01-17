HUD_SVGS = \
	hud/down.svg \
	hud/left.svg \
	hud/player_select.svg \
	hud/push.svg \
	hud/right.svg \
	hud/shout.svg \
	hud/title.svg \
	hud/up.svg \
	$(NULL)

HUD_PNGS = $(HUD_SVGS:.svg=.png)

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
	paving.png \
	$(NULL)

map-texture.png : make-map-texture.py $(BLOCK_IMAGES)
	$(AM_V_GEN)python3 $(srcdir)/make-map-texture.py $@ $(BLOCK_IMAGES)

imagesdir = $(datadir)/finvenkisto
dist_images_DATA = \
	ARTCREDITS \
	$(SHADERS) \
	$(MODELS) \
	$(OTHER_PNGS) \
	bambo1.png \
	bambo2.png \
	bambo3.png \
	finvenkisto.png \
	map-texture.png \
	hud.png \
	$(NULL)

EXTRA_DIST = \
	$(HUD_SVGS) \
	$(HUD_PNGS) \
	$(OTHER_SVGS) \
	$(OTHER_PNGS) \
	hud.png \
	hud-layout.h \
	make-atlas.py \
	make-map-texture.py \
	nekrokodilu.svg \
	$(MODELS:.ply=.blend) \
	$(BLOCK_IMAGES) \
	$(NULL)

.svg.png :
	$(AM_V_GEN)inkscape -e $@ $<

hud.png hud-layout.h : $(HUD_PNGS) make-atlas.py
	$(AM_V_GEN)python3 $(srcdir)/make-atlas.py \
	hud.png hud-layout.h $(HUD_PNGS)
