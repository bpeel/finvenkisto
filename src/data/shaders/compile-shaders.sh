#!/bin/bash

set -e

base_dir=$(dirname "$0")

if test -z "$GLSLC"; then
    GLSLC="$base_dir/../../../../shaderc/build/glslc/glslc"
fi

VERTEX_SHADERS=( \
    fv-map-vertex.glsl \
)

FRAGMENT_SHADERS=( \
    fv-lighting-texture-fragment.glsl \
)

function compile_shaders() {
    local stage="$1"; shift
    local spirv

    for x in "$@"; do
        spirv=$(echo "$x" | sed 's/glsl$/spirv/')
        $GLSLC \
            -I "$base_dir" \
            -fshader-stage="$stage" \
            -o "$base_dir/$spirv" \
            "$base_dir/$x"
    done
}

compile_shaders vertex "${VERTEX_SHADERS[@]}"
compile_shaders fragment "${FRAGMENT_SHADERS[@]}"
