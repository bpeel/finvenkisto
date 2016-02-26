#!/bin/bash

set -e

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
        ../../../../shaderc/build/glslc/glslc \
            -fshader-stage="$stage" \
            -o "$spirv" \
            "$x"
    done
}

compile_shaders vertex "${VERTEX_SHADERS[@]}"
compile_shaders fragment "${FRAGMENT_SHADERS[@]}"
