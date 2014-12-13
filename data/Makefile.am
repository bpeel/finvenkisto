SHADERS = \
	fv-simple-fragment.glsl \
	fv-simple-vertex.glsl \
	fv-map-fragment.glsl \
	fv-map-vertex.glsl \
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
	$(NULL)

map-texture.png : make-map-texture.py $(BLOCK_IMAGES)
	$(AM_V_GEN)python3 $(srcdir)/make-map-texture.py $@ $(BLOCK_IMAGES)

imagesdir = $(datadir)/finvenkisto
dist_images_DATA = \
	$(SHADERS) \
	person.ply \
	map-texture.png \
	$(NULL)

EXTRA_DIST = \
	ARTCREDITS \
	make-map-texture.py \
	person.blend \
	$(BLOCK_IMAGES) \
	$(NULL)
