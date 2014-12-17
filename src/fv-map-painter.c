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

#include <epoxy/gl.h>
#include <math.h>
#include <limits.h>
#include <assert.h>

#include "fv-map-painter.h"
#include "fv-map.h"
#include "fv-util.h"
#include "fv-buffer.h"
#include "fv-image.h"

#define FV_MAP_PAINTER_TILE_WIDTH 8
#define FV_MAP_PAINTER_TILE_HEIGHT 8

#define FV_MAP_PAINTER_TILES_X (FV_MAP_WIDTH / FV_MAP_PAINTER_TILE_WIDTH)
#define FV_MAP_PAINTER_TILES_Y (FV_MAP_HEIGHT / FV_MAP_PAINTER_TILE_HEIGHT)

_Static_assert(FV_MAP_WIDTH % FV_MAP_PAINTER_TILE_WIDTH == 0,
               "The map size must be a multiple of the tile size");
_Static_assert(FV_MAP_HEIGHT % FV_MAP_PAINTER_TILE_HEIGHT == 0,
               "The map size must be a multiple of the tile size");

#define FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE 64

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

        GLuint texture;
        int texture_width, texture_height;
};

struct vertex {
        float x, y, z;
        float s, t;
};

struct tile_data {
        struct fv_buffer indices;
        struct fv_buffer vertices;
};

static float
get_block_height(fv_map_block_t block)
{
        if (FV_MAP_IS_FULL_WALL(block))
                return 2.0f;

        if (FV_MAP_IS_HALF_WALL(block))
                return 1.0f;

        return 0.0f;
}

static float
get_position_height(int x, int y)
{
        if (x < 0 || x >= FV_MAP_WIDTH ||
            y < 0 || y >= FV_MAP_HEIGHT)
                return 0.0f;

        return get_block_height(fv_map[y * FV_MAP_WIDTH + x]);
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

static struct vertex *
add_horizontal_side(struct tile_data *data,
                    float y,
                    float x1, float z1,
                    float x2, float z2)
{
        struct vertex *v = reserve_quad(data);
        int i;

        for (i = 0; i < 4; i++)
                v[i].y = y;

        v[0].x = x1;
        v[0].z = z1;
        v[1].x = x2;
        v[1].z = z1;
        v[2].x = x1;
        v[2].z = z2;
        v[3].x = x2;
        v[3].z = z2;

        return v;
}

static struct vertex *
add_vertical_side(struct tile_data *data,
                  float x,
                  float y1, float z1,
                  float y2, float z2)
{
        struct vertex *v = reserve_quad(data);
        int i;

        for (i = 0; i < 4; i++)
                v[i].x = x;

        v[0].y = y1;
        v[0].z = z1;
        v[1].y = y2;
        v[1].z = z1;
        v[2].y = y1;
        v[2].z = z2;
        v[3].y = y2;
        v[3].z = z2;

        return v;
}

static void
set_tex_coords_for_image(struct fv_map_painter *painter,
                         struct vertex v[4],
                         int image,
                         float height)
{
        int blocks_w = (painter->texture_width /
                        FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE);
        int is1 = image % blocks_w * FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE;
        int it1 = image / blocks_w * FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE;
        float s1 = is1 / (float) painter->texture_width;
        float t1 = it1 / (float) painter->texture_height;
        float s2 = ((is1 + FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE) /
                    (float) painter->texture_width);
        float t2 = ((it1 + FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE * height) /
                    (float) painter->texture_height);

        v[0].s = s1;
        v[0].t = t2;
        v[1].s = s2;
        v[1].t = t2;
        v[2].s = s1;
        v[2].t = t1;
        v[3].s = s2;
        v[3].t = t1;
}

static void
generate_square(struct fv_map_painter *painter,
                struct tile_data *data,
                int x, int y)
{
        fv_map_block_t block = fv_map[y * FV_MAP_WIDTH + x];
        struct vertex *v;
        int i;
        float z, oz;

        v = reserve_quad(data);

        z = get_block_height(block);

        set_tex_coords_for_image(painter, v,
                                 FV_MAP_GET_BLOCK_TOP_IMAGE(block),
                                 1.0f);

        for (i = 0; i < 4; i++)
                v[i].z = z;

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
        if (z > (oz = get_position_height(x, y + 1))) {
                v = add_horizontal_side(data, y + 1, x + 1, oz, x, z);
                set_tex_coords_for_image(painter, v,
                                         FV_MAP_GET_BLOCK_NORTH_IMAGE(block),
                                         z - oz);
        }
        if (z > (oz = get_position_height(x, y - 1))) {
                v = add_horizontal_side(data, y, x, oz, x + 1, z);
                set_tex_coords_for_image(painter, v,
                                         FV_MAP_GET_BLOCK_SOUTH_IMAGE(block),
                                         z - oz);
        }
        if (z > (oz = get_position_height(x - 1, y))) {
                v = add_vertical_side(data, x, y + 1, oz, y, z);
                set_tex_coords_for_image(painter, v,
                                         FV_MAP_GET_BLOCK_WEST_IMAGE(block),
                                         z - oz);
        }
        if (z > (oz = get_position_height(x + 1, y))) {
                v = add_vertical_side(data, x + 1, y, oz, y + 1, z);
                set_tex_coords_for_image(painter, v,
                                         FV_MAP_GET_BLOCK_EAST_IMAGE(block),
                                         z - oz);
        }
}

static void
generate_tile(struct fv_map_painter *painter,
              struct tile_data *data,
              int tx, int ty)
{
        int x, y;

        for (y = 0; y < FV_MAP_PAINTER_TILE_HEIGHT; y++) {
                for (x = 0; x < FV_MAP_PAINTER_TILE_WIDTH; x++) {
                        generate_square(painter,
                                        data,
                                        tx * FV_MAP_PAINTER_TILE_WIDTH + x,
                                        ty * FV_MAP_PAINTER_TILE_HEIGHT + y);
                }
        }

}

struct fv_map_painter *
fv_map_painter_new(struct fv_shader_data *shader_data)
{
        struct fv_map_painter *painter;
        struct tile_data data;
        struct fv_map_painter_tile *tile;
        int first, tx, ty, i;
        uint8_t *tex_data;
        int tex_width, tex_height;
        GLuint tex_uniform;

        tex_data = fv_image_load("map-texture.png",
                                 &tex_width, &tex_height,
                                 3 /* components */);
        if (tex_data == NULL)
                return NULL;

        painter = fv_alloc(sizeof *painter);
        painter->program = shader_data->programs[FV_SHADER_DATA_PROGRAM_MAP];
        painter->transform_uniform =
                glGetUniformLocation(painter->program, "transform");

        painter->texture_width = tex_width;
        painter->texture_height = tex_height;

        glGenTextures(1, &painter->texture);
        glBindTexture(GL_TEXTURE_2D, painter->texture);
        glTexImage2D(GL_TEXTURE_2D,
                     0, /* level */
                     GL_RGB,
                     tex_width, tex_height,
                     0, /* border */
                     GL_RGB,
                     GL_UNSIGNED_BYTE,
                     tex_data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D,
                        GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,
                        GL_TEXTURE_MAG_FILTER,
                        GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        fv_free(tex_data);

        tex_uniform = glGetUniformLocation(painter->program, "tex");
        glUseProgram(painter->program);
        glUniform1i(tex_uniform, 0);

        fv_buffer_init(&data.vertices);
        fv_buffer_init(&data.indices);

        tile = painter->tiles;

        for (ty = 0; ty < FV_MAP_PAINTER_TILES_Y; ty++) {
                for (tx = 0; tx < FV_MAP_PAINTER_TILES_X; tx++) {
                        first = data.indices.length / sizeof (uint16_t);
                        tile->min = (data.vertices.length /
                                     sizeof (struct vertex));
                        tile->offset = data.indices.length;
                        generate_tile(painter, &data, tx, ty);
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

        assert(data.vertices.length / sizeof (struct vertex) < 65536);

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
                              2, /* size */
                              GL_FLOAT,
                              GL_FALSE, /* normalized */
                              sizeof (struct vertex),
                              (void *) (intptr_t)
                              offsetof(struct vertex, s));

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

        glBindTexture(GL_TEXTURE_2D, painter->texture);

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
        glDeleteTextures(1, &painter->texture);
        glDeleteVertexArrays(1, &painter->array);
        glDeleteBuffers(1, &painter->buffer);
        fv_free(painter);
}
