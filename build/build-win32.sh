#!/bin/bash

set -e

SDL_VERSION=2.0.3

SDL_FILENAME=SDL2-devel-${SDL_VERSION}-mingw.tar.gz
SDL_URL="http://libsdl.org/release/$SDL_FILENAME"

CONFIG_GUESS_URL="http://git.savannah.gnu.org/gitweb/?p=automake.git;a=blob_plain;f=lib/config.guess"

GL_HEADER_URLS=( \
    http://cgit.freedesktop.org/mesa/mesa/plain/include/GL/gl.h \
    http://www.opengl.org/registry/api/glext.h );

GL_HEADERS=( gl.h glext.h );

SRC_BUILD_DIR=`dirname "$0"`
SRC_DIR=`cd "$SRC_BUILD_DIR"/.. && pwd`
DOWNLOADS_DIR="$SRC_DIR/downloads"
DEPS_DIR="$SRC_DIR/deps"
INSTALL_DIR="$SRC_DIR/install"
RESULT_FILE="$SRC_DIR/finvenkisto.zip"
RESULT_DIR="$SRC_DIR/result"

function find_compiler ()
{
    local gccbin fullpath;

    if [ -z "$MINGW_TOOL_PREFIX" ]; then
	for gccbin in i{3,4,5,6}86{-pc,-w64,}-mingw32{,msvc}-gcc; do
	    fullpath=`which $gccbin 2>/dev/null || true`;
	    if [ -n "$fullpath" -a -e "$fullpath" ]; then
		MINGW_TOOL_PREFIX="${fullpath%%gcc}";
		break;
	    fi;
	done;
	if [ -z "$MINGW_TOOL_PREFIX" ]; then
	    echo;
	    echo "No suitable cross compiler was found.";
	    echo;
	    echo "If you already have a compiler installed,";
	    echo "please set the MINGW_TOOL_PREFIX variable";
	    echo "to point to its location without the";
	    echo "gcc suffix (eg: \"/usr/bin/i386-mingw32-\").";
	    echo;
	    echo "If you are using Ubuntu, you can install a";
	    echo "compiler by typing:";
	    echo;
	    echo " sudo apt-get install mingw32";
	    echo;
	    echo "Otherwise you can try following the instructions here:";
	    echo;
	    echo " http://www.libsdl.org/extras/win32/cross/README.txt";

	    exit 1;
	fi;
    fi;

    TARGET="${MINGW_TOOL_PREFIX##*/}";
    TARGET="${TARGET%%-}";

    echo "Using compiler ${MINGW_TOOL_PREFIX}gcc and target $TARGET";
}

mkdir -p "$DOWNLOADS_DIR"

rm -rf "$DEPS_DIR"
rm -rf "$INSTALL_DIR"
rm -rf "$RESULT_DIR" "$RESULT_FILE"
mkdir -p "$DEPS_DIR"
mkdir -p "$INSTALL_DIR/include/GL"
mkdir -p "$RESULT_DIR"

function do_download ()
{
    local local_fn="$DOWNLOADS_DIR/$2"
    if ! test -f "$local_fn"; then
        curl -L -o "$local_fn" "$1"
    fi
}

do_download "$SDL_URL" "$SDL_FILENAME"
do_download "$CONFIG_GUESS_URL" "config.guess"

for dep in "${GL_HEADER_URLS[@]}"; do
    bn="${dep##*/}";
    do_download "$dep" "$bn";
done

for header in "${GL_HEADERS[@]}"; do
    cp "$DOWNLOADS_DIR/$header" "$INSTALL_DIR/include/GL/"
done

find_compiler
BUILD=`bash $DOWNLOADS_DIR/config.guess`

RUN_PKG_CONFIG="$DEPS_DIR/run-pkg-config.sh";

echo "Generating $DEPS_DIR/run-pkg-config.sh";

cat > "$RUN_PKG_CONFIG" <<EOF
# This is a wrapper script for pkg-config that overrides the
# PKG_CONFIG_LIBDIR variable so that it won't pick up the local system
# .pc files.

# The MinGW compiler on Fedora tries to do a similar thing except that
# it also unsets PKG_CONFIG_PATH. This breaks any attempts to add a
# local search path so we need to avoid using that script.

export PKG_CONFIG_LIBDIR="$INSTALL_DIR/lib/pkgconfig"

exec pkg-config "\$@"
EOF

chmod a+x "$RUN_PKG_CONFIG";

tar -vxf "$DOWNLOADS_DIR/$SDL_FILENAME" -C "$INSTALL_DIR" \
    "SDL2-$SDL_VERSION/i686-w64-mingw32" \
    --strip-components=2

sed -i "s|^prefix=.*|prefix=${INSTALL_DIR}|" \
    "$INSTALL_DIR/lib/pkgconfig/sdl2.pc"

./autogen.sh --prefix="$INSTALL_DIR" \
    --host="$TARGET" \
    --target="$TARGET" \
    --build="$BUILD" \
    CFLAGS="-mms-bitfields -I$INSTALL_DIR/include" \
    PKG_CONFIG="$RUN_PKG_CONFIG"

make -j4
make install

cp "$INSTALL_DIR/bin/"{finvenkisto.exe,SDL2.dll} "$RESULT_DIR"
cp -R "$INSTALL_DIR/share/finvenkisto" "$RESULT_DIR/data"

cd "$RESULT_DIR"
zip -r "$RESULT_FILE" *
