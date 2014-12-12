/*
 * Finvenkisto
 *
 * Copyright (C) 2014 Neil Roberts
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

#include <math.h>
#include <limits.h>

#include "fv-map-painter.h"
#include "fv-map.h"
#include "fv-util.h"
#include "fv-buffer.h"

#define FV_MAP_PAINTER_TILE_WIDTH 8
#define FV_MAP_PAINTER_TILE_HEIGHT 8

#define FV_MAP_PAINTER_TILES_X (FV_MAP_WIDTH / FV_MAP_PAINTER_TILE_WIDTH)
#define FV_MAP_PAINTER_TILES_Y (FV_MAP_HEIGHT / FV_MAP_PAINTER_TILE_HEIGHT)

_Static_assert(FV_MAP_WIDTH % FV_MAP_PAINTER_TILE_WIDTH == 0,
               "The map size must be a multiple of the tile size");
_Static_assert(FV_MAP_HEIGHT % FV_MAP_PAINTER_TILE_HEIGHT == 0,
               "The map size must be a multiple of the tile size");

struct fv_map_painter_tile {
        size_t offset;
        int count;
        int min, max;
};

struct fv_map_painter {
        GLuint buffer;
        GLuint array;
        struct fv_map_painter_tile tiles[FV_MAP_PAINTER_TILES_X *
                                         FV_MAP_PAINTER_TILES_Y];
        GLuint program;
        GLuint transform_uniform;
};

struct vertex {
        float x, y, z;
        uint32_t color;
};

struct tile_data {
        struct fv_buffer indices;
        struct fv_buffer vertices;
};

static float
get_tile_height(uint8_t tile)
{
        if (FV_MAP_IS_FULL_WALL(tile))
                return 2.0f;

        if (FV_MAP_IS_HALF_WALL(tile))
                return 1.0f;

        return 0.0f;
}

static float
get_position_height(int x, int y)
{
        if (x < 0 || x >= FV_MAP_WIDTH ||
            y < 0 || y >= FV_MAP_HEIGHT)
                return 0.0f;

        return get_tile_height(fv_map[y * FV_MAP_WIDTH + x]);
}

static struct vertex *
reserve_quad(struct tile_data *data)
{
        struct vertex *v;
        uint16_t *idx;
        size_t v1, i1;

        v1 = data->vertices.length / sizeof (struct vertex);
        fv_buffer_set_length(&data->vertices,
                             sizeof (struct vertex) * (v1 + 4));
        v = (struct vertex *) data->vertices.data + v1;

        i1 = data->indices.length / sizeof (uint16_t);
        fv_buffer_set_length(&data->indices,
                             sizeof (uint16_t) * (i1 + 6));
        idx = (uint16_t *) data->indices.data + i1;

        *(idx++) = v1 + 0;
        *(idx++) = v1 + 1;
        *(idx++) = v1 + 2;
        *(idx++) = v1 + 2;
        *(idx++) = v1 + 1;
        *(idx++) = v1 + 3;

        return v;
}

static void
add_horizontal_side(struct tile_data *data,
                    float y,
                    float x1, float z1,
                    float x2, float z2)
{
        struct vertex *v = reserve_quad(data);
        int i;

        for (i = 0; i < 4; i++) {
                v[i].color = FV_UINT32_TO_BE(0xff00ffff);
                v[i].y = y;
        }

        v->x = x1;
        v->z = z1;
        v++;
        v->x = x2;
        v->z = z1;
        v++;
        v->x = x1;
        v->z = z2;
        v++;
        v->x = x2;
        v->z = z2;
}

static void
add_vertical_side(struct tile_data *data,
                  float x,
                  float y1, float z1,
                  float y2, float z2)
{
        struct vertex *v = reserve_quad(data);
        int i;

        for (i = 0; i < 4; i++) {
                v[i].color = FV_UINT32_TO_BE(0xff00ffff);
                v[i].x = x;
        }

        v->y = y1;
        v->z = z1;
        v++;
        v->y = y2;
        v->z = z1;
        v++;
        v->y = y1;
        v->z = z2;
        v++;
        v->y = y2;
        v->z = z2;
}

static void
generate_square(struct tile_data *data,
                int x, int y)
{
        uint8_t tile = fv_map[y * FV_MAP_WIDTH + x];
        struct vertex *v;
        uint32_t color;
        int i;
        float z, oz;

        v = reserve_quad(data);

        z = get_tile_height(tile);

        if (z >= 2.0f)
                color = FV_UINT32_TO_BE(0xff0000ff);
        else if (z >= 1.0f)
                color = FV_UINT32_TO_BE(0x00ff00ff);
        else
                color = FV_UINT32_TO_BE(0x8080ffff);

        for (i = 0; i < 4; i++) {
                v[i].z = z;
                v[i].color = color;
        }

        v->x = x;
        v->y = y;
        v++;
        v->x = x + 1;
        v->y = y;
        v++;
        v->x = x;
        v->y = y + 1;
        v++;
        v->x = x + 1;
        v->y = y + 1;

        /* Add the side walls */
        if (z > (oz = get_position_height(x, y + 1)))
                add_horizontal_side(data, y + 1, x, z, x + 1, oz);
        if (z > (oz = get_position_height(x, y - 1)))
                add_horizontal_side(data, y, x, oz, x + 1, z);
        if (z > (oz = get_position_height(x - 1, y)))
                add_vertical_side(data, x, y, z, y + 1, oz);
        if (z > (oz = get_position_height(x + 1, y)))
                add_vertical_side(data, x + 1, y, oz, y + 1, z);
}

static void
generate_tile(struct tile_data *data,
              int tx, int ty)
{
        int x, y;

        for (y = 0; y < FV_MAP_PAINTER_TILE_HEIGHT; y++) {
                for (x = 0; x < FV_MAP_PAINTER_TILE_WIDTH; x++) {
                        generate_square(data,
                                        tx * FV_MAP_PAINTER_TILES_X + x,
                                        ty * FV_MAP_PAINTER_TILES_Y + y);
                }
        }

}

struct fv_map_painter *
fv_map_painter_new(struct fv_shader_data *shader_data)
{
        struct fv_map_painter *painter = fv_alloc(sizeof *painter);
        struct tile_data data;
        struct fv_map_painter_tile *tile = painter->tiles;
        int first, tx, ty, i;

        painter->program = shader_data->programs[FV_SHADER_DATA_PROGRAM_SIMPLE];
        painter->transform_uniform =
                glGetUniformLocation(painter->program, "transform");

        fv_buffer_init(&data.vertices);
        fv_buffer_init(&data.indices);

        for (ty = 0; ty < FV_MAP_PAINTER_TILES_Y; ty++) {
                for (tx = 0; tx < FV_MAP_PAINTER_TILES_X; tx++) {
                        first = data.indices.length / sizeof (uint16_t);
                        tile->min = (data.vertices.length /
                                     sizeof (struct vertex));
                        tile->offset = data.indices.length;
                        generate_tile(&data, tx, ty);
                        tile->max = (data.vertices.length /
                                     sizeof (struct vertex)) - 1;
                        tile->count = (data.indices.length /
                                       sizeof (uint16_t) -
                                       first);
                        tile++;
                }
        }

        for (i = 0; i < FV_MAP_PAINTER_TILES_X * FV_MAP_PAINTER_TILES_Y; i++)
                painter->tiles[i].offset += data.vertices.length;

        glGenVertexArrays(1, &painter->array);
        glBindVertexArray(painter->array);

        glGenBuffers(1, &painter->buffer);
        glBindBuffer(GL_ARRAY_BUFFER, painter->buffer);
        glBufferData(GL_ARRAY_BUFFER,
                     data.vertices.length +
                     data.indices.length,
                     NULL, /* data */
                     GL_STATIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER,
                        0, /* offset */
                        data.vertices.length,
                        data.vertices.data);
        glBufferSubData(GL_ARRAY_BUFFER,
                        data.vertices.length, /* offset */
                        data.indices.length,
                        data.indices.data);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, /* index */
                              3, /* size */
                              GL_FLOAT,
                              GL_FALSE, /* normalized */
                              sizeof (struct vertex),
                              (void *) (intptr_t)
                              offsetof(struct vertex, x));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, /* index */
                              3, /* size */
                              GL_UNSIGNED_BYTE,
                              GL_TRUE, /* normalized */
                              sizeof (struct vertex),
                              (void *) (intptr_t)
                              offsetof(struct vertex, color));

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, painter->buffer);

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        fv_buffer_destroy(&data.indices);
        fv_buffer_destroy(&data.vertices);

        return painter;
}

void
fv_map_painter_paint(struct fv_map_painter *painter,
                     struct fv_logic *logic,
                     float visible_w,
                     float visible_h,
                     const struct fv_transform *transform)
{
        float center_x, center_y;
        int x_min, x_max, y_min, y_max;
        int idx_min;
        int idx_max;
        const struct fv_map_painter_tile *tile = NULL;
        int count;
        int y, x;

        fv_logic_get_center(logic, &center_x, &center_y);

        x_min = floorf((center_x - visible_w / 2.0f) /
                       FV_MAP_PAINTER_TILE_WIDTH);
        x_max = ceilf((center_x + visible_w / 2.0f) /
                      FV_MAP_PAINTER_TILE_WIDTH);
        y_min = floorf((center_y - visible_h / 2.0f) /
                       FV_MAP_PAINTER_TILE_HEIGHT);
        y_max = ceilf((center_y + visible_h / 2.0f) /
                      FV_MAP_PAINTER_TILE_HEIGHT);

        if (x_min < 0)
                x_min = 0;
        if (x_max > FV_MAP_PAINTER_TILES_X)
                x_max = FV_MAP_PAINTER_TILES_X;
        if (y_min < 0)
                y_min = 0;
        if (y_max > FV_MAP_PAINTER_TILES_Y)
                y_max = FV_MAP_PAINTER_TILES_Y;

        if (y_min >= y_max || x_min >= x_max)
                return;

        glUseProgram(painter->program);
        glUniformMatrix4fv(painter->transform_uniform,
                           1, /* count */
                           GL_FALSE, /* transpose */
                           &transform->mvp.xx);

        glEnable(GL_DEPTH_TEST);

        glBindVertexArray(painter->array);

        for (y = y_min; y < y_max; y++) {
                count = 0;
                idx_min = INT_MAX;
                idx_max = INT_MIN;

                for (x = x_max - 1; x >= x_min; x--) {
                        tile = painter->tiles +
                                y * FV_MAP_PAINTER_TILES_X + x;
                        count += tile->count;
                        if (tile->min < idx_min)
                                idx_min = tile->min;
                        if (tile->max > idx_max)
                                idx_max = tile->max;
                }

                glDrawRangeElements(GL_TRIANGLES,
                                    idx_min, idx_max,
                                    count,
                                    GL_UNSIGNED_SHORT,
                                    (void *) (intptr_t)
                                    tile->offset);
        }

        glDisable(GL_DEPTH_TEST);
}

void
fv_map_painter_free(struct fv_map_painter *painter)
{
        glDeleteVertexArrays(1, &painter->array);
        glDeleteBuffers(1, &painter->buffer);
        fv_free(painter);
}
