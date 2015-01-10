SHADERS = \
	fv-color-fragment.glsl \
	fv-color-vertex.glsl \
	fv-texture-fragment.glsl \
	fv-texture-vertex.glsl \
	$(NULL)

MODELS = \
	person.ply \
	table.ply \
	toilet.ply \
	teaset.ply \
	$(NULL)

# The order here must match the order for the image numbers used in
# the map
BLOCK_IMAGES = \
	walltop.png \
	parquet.png \
	paving.png \
	bathroom-floor.png \
	brick-flooring.png \
	grass.png \
	brick-wall.png \
	inner-wall.png \
	bathroom-wall.png \
	bathroom-wall-note.png \
	$(NULL)

map-texture.png : make-map-texture.py $(BLOCK_IMAGES)
	$(AM_V_GEN)python3 $(srcdir)/make-map-texture.py $@ $(BLOCK_IMAGES)

imagesdir = $(datadir)/finvenkisto
dist_images_DATA = \
	ARTCREDITS \
	$(SHADERS) \
	$(MODELS) \
	person.png \
	map-texture.png \
	$(NULL)

EXTRA_DIST = \
	make-map-texture.py \
	$(MODELS:.ply=.blend) \
	$(BLOCK_IMAGES) \
	$(NULL)
