/*
 * Finvenkisto
 *
 * Copyright (C) 2015 Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include "fv-gl.h"
#include "fv-map-buffer.h"
#include "fv-buffer.h"

static struct
{
        GLenum target;
        GLintptr offset;
        GLsizeiptr length;
        bool flush_explicit;
        bool using_buffer;
        struct fv_buffer buffer;
} fv_map_buffer_state = {
        .buffer = FV_BUFFER_STATIC_INIT
};

void *
fv_map_buffer_map(GLenum target,
                  GLintptr offset,
                  GLsizeiptr length,
                  bool flush_explicit)
{
        GLbitfield flags;
        void *ret = NULL;

        fv_map_buffer_state.target = target;
        fv_map_buffer_state.offset = offset;
        fv_map_buffer_state.length = length;
        fv_map_buffer_state.flush_explicit = flush_explicit;

        if (fv_gl.have_map_buffer_range) {
                flags = (GL_MAP_WRITE_BIT |
                         GL_MAP_INVALIDATE_BUFFER_BIT);
                if (flush_explicit)
                        flags |= GL_MAP_FLUSH_EXPLICIT_BIT;
                ret = fv_gl.glMapBufferRange(target,
                                             offset,
                                             length,
                                             flags);
                if (ret) {
                        fv_map_buffer_state.using_buffer = false;
                        return ret;
                }
        }

        fv_map_buffer_state.using_buffer = true;

        fv_buffer_set_length(&fv_map_buffer_state.buffer, length);

        return fv_map_buffer_state.buffer.data;
}

void
fv_map_buffer_flush(GLintptr offset,
                    GLsizeiptr length)
{
        if (fv_map_buffer_state.using_buffer) {
                fv_gl.glBufferSubData(fv_map_buffer_state.target,
                                      fv_map_buffer_state.offset + offset,
                                      length,
                                      fv_map_buffer_state.buffer.data + offset);
        } else {
                fv_gl.glFlushMappedBufferRange(fv_map_buffer_state.target,
                                               offset,
                                               length);
        }
}

void
fv_map_buffer_unmap(void)
{
        if (fv_map_buffer_state.using_buffer) {
                if (!fv_map_buffer_state.flush_explicit)
                        fv_gl.glBufferSubData(fv_map_buffer_state.target,
                                              fv_map_buffer_state.offset,
                                              fv_map_buffer_state.length,
                                              fv_map_buffer_state.buffer.data);
        } else {
                fv_gl.glUnmapBuffer(fv_map_buffer_state.target);
        }
}
