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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "fv-shader-data.h"
#include "fv-util.h"
#include "fv-data.h"
#include "fv-buffer.h"
#include "fv-gl.h"
#include "fv-error-message.h"

static const char
fv_shader_data_version[] =
        "#version 110\n";

static const char
fv_shader_data_have_texture_2d_array[] =
        "#extension GL_EXT_texture_array : require\n"
        "#define HAVE_TEXTURE_2D_ARRAY 1\n";

static const char
fv_shader_data_have_instanced_arrays[] =
        "#define HAVE_INSTANCED_ARRAYS 1\n";

static const char
fv_shader_data_newline[] =
        "\n";

struct fv_shader_data_shader {
        GLenum type;
        const char **filenames;
        enum fv_shader_data_program programs[FV_SHADER_DATA_N_PROGRAMS + 1];
};

#define PROGRAMS_END FV_SHADER_DATA_N_PROGRAMS

static const struct fv_shader_data_shader
fv_shader_data_shaders[] = {
        {
                GL_FRAGMENT_SHADER,
                (const char *[]) { "fv-texture-fragment.glsl", NULL },
                {
                        FV_SHADER_DATA_PROGRAM_HUD,
                        FV_SHADER_DATA_PROGRAM_TEXTURE,
                        PROGRAMS_END
                }
        },
        {
                GL_VERTEX_SHADER,
                (const char *[]) { "fv-texture-vertex.glsl", NULL },
                { FV_SHADER_DATA_PROGRAM_TEXTURE, PROGRAMS_END }
        },
        {
                GL_FRAGMENT_SHADER,
                (const char *[]) { "fv-color-fragment.glsl", NULL },
                { FV_SHADER_DATA_PROGRAM_SPECIAL_COLOR, PROGRAMS_END }
        },
        {
                GL_VERTEX_SHADER,
                (const char *[]) {
                        "fv-lighting.glsl",
                        "fv-special-color-vertex.glsl",
                        NULL
                },
                { FV_SHADER_DATA_PROGRAM_SPECIAL_COLOR, PROGRAMS_END }
        },
        {
                GL_FRAGMENT_SHADER,
                (const char *[]) { "fv-lighting-texture-fragment.glsl", NULL },
                {
                        FV_SHADER_DATA_PROGRAM_SPECIAL_TEXTURE,
                        FV_SHADER_DATA_PROGRAM_MAP,
                        PROGRAMS_END
                }
        },
        {
                GL_VERTEX_SHADER,
                (const char *[]) {
                        "fv-lighting.glsl",
                        "fv-special-texture-vertex.glsl",
                        NULL
                },
                { FV_SHADER_DATA_PROGRAM_SPECIAL_TEXTURE, PROGRAMS_END }
        },
        {
                GL_FRAGMENT_SHADER,
                (const char *[]) { "fv-person-fragment.glsl", NULL },
                { FV_SHADER_DATA_PROGRAM_PERSON, PROGRAMS_END }
        },
        {
                GL_VERTEX_SHADER,
                (const char *[]) {
                        "fv-lighting.glsl",
                        "fv-person-vertex.glsl",
                        NULL
                },
                { FV_SHADER_DATA_PROGRAM_PERSON, PROGRAMS_END }
        },
        {
                GL_VERTEX_SHADER,
                (const char *[]) { "fv-hud-vertex.glsl", NULL },
                { FV_SHADER_DATA_PROGRAM_HUD, PROGRAMS_END }
        },
        {
                GL_VERTEX_SHADER,
                (const char *[]) {
                        "fv-lighting.glsl",
                        "fv-map-vertex.glsl",
                        NULL
                },
                { FV_SHADER_DATA_PROGRAM_MAP, PROGRAMS_END }
        }
};

static GLuint
create_shader(const char *name,
              GLenum type,
              const char *source,
              int source_length)
{
        GLuint shader;
        GLint length, compile_status;
        GLsizei actual_length;
        GLchar *info_log;
        const char *source_strings[6];
        GLint lengths[FV_N_ELEMENTS(source_strings)];
        int n_strings = 0;

        shader = fv_gl.glCreateShader(type);

        source_strings[n_strings] = fv_shader_data_version;
        lengths[n_strings++] = sizeof fv_shader_data_version - 1;

        if (fv_gl.have_texture_2d_array) {
                source_strings[n_strings] =
                        fv_shader_data_have_texture_2d_array;
                lengths[n_strings++] =
                        sizeof fv_shader_data_have_texture_2d_array - 1;
        }

        if (fv_gl.have_instanced_arrays) {
                source_strings[n_strings] =
                        fv_shader_data_have_instanced_arrays;
                lengths[n_strings++] =
                        sizeof fv_shader_data_have_instanced_arrays - 1;
        }

        source_strings[n_strings] = fv_shader_data_newline;
        lengths[n_strings++] = sizeof fv_shader_data_newline - 1;

        source_strings[n_strings] = source;
        lengths[n_strings++] = source_length;
        fv_gl.glShaderSource(shader,
                             n_strings,
                             (const GLchar **) source_strings,
                             lengths);

        fv_gl.glCompileShader(shader);

        fv_gl.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

        if (length > 0) {
                info_log = malloc(length);
                fv_gl.glGetShaderInfoLog(shader, length,
                                         &actual_length,
                                         info_log);
                if (*info_log)
                        fprintf(stderr,
                                "Info log for %s:\n%s\n",
                                name, info_log);
                free(info_log);
        }

        fv_gl.glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);

        if (!compile_status) {
                fv_error_message("%s compilation failed", name);
                fv_gl.glDeleteShader(shader);
                return 0;
        }

        return shader;
}

static GLuint
create_shader_from_files(GLenum shader_type,
                         const char **filenames)
{
        char *fullname;
        char *source, *p;
        long int *lengths;
        long int total_length = 0;
        size_t got;
        GLuint shader;
        int n_files;
        FILE **files;
        int i;

        for (n_files = 0, i = 0; filenames[i]; i++)
                n_files++;

        files = alloca(sizeof files[0] * n_files);
        lengths = alloca(sizeof lengths[0] * n_files);

        for (n_files = 0, i = 0; filenames[i]; i++) {
                fullname = fv_data_get_filename(filenames[i]);

                if (fullname == NULL) {
                        fv_error_message("Couldn't get filename for %s",
                                         filenames[i]);
                        shader = 0;
                        goto out;
                }

                files[i] = fopen(fullname, "r");

                fv_free(fullname);

                if (files[i] == NULL) {
                        fv_error_message("%s: %s",
                                         filenames[i],
                                         strerror(errno));
                        shader = 0;
                        goto out;
                }

                n_files++;

                if (fseek(files[i], 0, SEEK_END) != 0)
                        goto file_error;

                lengths[i] = ftell(files[i]);
                if (lengths[i] == -1)
                        goto file_error;

                if (fseek(files[i], 0, SEEK_SET) != 0)
                        goto file_error;

                total_length += lengths[i];
        }

        source = fv_alloc(total_length);

        for (p = source, i = 0; i < n_files; i++) {
                got = fread(p, 1, lengths[i], files[i]);

                if (got != lengths[i]) {
                        shader = 0;
                        fv_free(source);
                        if (ferror(files[i]))
                                goto file_error;
                        fv_error_message("%s: Unexpected EOF",
                                         filenames[i]);
                        goto out;
                }

                p += got;
        }

        shader = create_shader(filenames[n_files - 1],
                               shader_type,
                               source,
                               total_length);

        fv_free(source);

        goto out;

file_error:
        fv_error_message("%s: %s", filenames[i], strerror(errno));
        shader = 0;
out:
        for (i = 0; i < n_files; i++)
                fclose(files[i]);

        return shader;
}

static bool
shader_contains_program(const struct fv_shader_data_shader *shader,
                        enum fv_shader_data_program program_num)
{
        int i;

        for (i = 0; shader->programs[i] != PROGRAMS_END; i++) {
                if (shader->programs[i] == program_num)
                        return true;
        }

        return false;
}

static char *
get_program_name(enum fv_shader_data_program program_num)
{
        struct fv_buffer buffer = FV_BUFFER_STATIC_INIT;
        const struct fv_shader_data_shader *shader;
        int i, j;

        /* Generate the program name as just a list of the shaders it
         * contains */

        fv_buffer_append_c(&buffer, '(');

        for (i = 0; i < FV_N_ELEMENTS(fv_shader_data_shaders); i++) {
                shader = fv_shader_data_shaders + i;

                if (shader_contains_program(shader, program_num)) {
                        if (buffer.length > 1)
                                fv_buffer_append_string(&buffer, ", ");
                        for (j = 0; shader->filenames[j]; j++)
                                fv_buffer_append_string(&buffer,
                                                        shader->filenames[j]);
                }
        }

        fv_buffer_append_string(&buffer, ")");

        return (char *) buffer.data;
}

static bool
link_program(struct fv_shader_data *data,
             enum fv_shader_data_program program_num)
{
        GLint length, link_status;
        GLsizei actual_length;
        GLchar *info_log;
        GLuint program;
        char *program_name;

        program = data->programs[program_num];

        fv_gl.glBindAttribLocation(program,
                                   FV_SHADER_DATA_ATTRIB_POSITION,
                                   "position");
        fv_gl.glBindAttribLocation(program,
                                   FV_SHADER_DATA_ATTRIB_TEX_COORD,
                                   "tex_coord_attrib");
        fv_gl.glBindAttribLocation(program,
                                   FV_SHADER_DATA_ATTRIB_NORMAL,
                                   "normal_attrib");
        fv_gl.glBindAttribLocation(program,
                                   FV_SHADER_DATA_ATTRIB_COLOR,
                                   "color_attrib");

        fv_gl.glLinkProgram(program);

        fv_gl.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);

        if (length > 0) {
                info_log = malloc(length);
                fv_gl.glGetProgramInfoLog(program, length,
                                          &actual_length,
                                          info_log);
                if (*info_log) {
                        program_name = get_program_name(program_num);
                        fprintf(stderr, "Link info log for %s:\n%s\n",
                                program_name,
                                info_log);
                        fv_free(program_name);
                }
                free(info_log);
        }

        fv_gl.glGetProgramiv(program, GL_LINK_STATUS, &link_status);

        if (!link_status) {
                program_name = get_program_name(program_num);
                fv_error_message("%s program link failed", program_name);
                fv_free(program_name);
                return false;
        }

        return true;
}

static bool
link_programs(struct fv_shader_data *data)
{
        int i;

        for (i = 0; i < FV_SHADER_DATA_N_PROGRAMS; i++) {
                if (!link_program(data, i))
                        return false;
        }

        return true;
}

bool
fv_shader_data_init(struct fv_shader_data *data)
{
        const struct fv_shader_data_shader *shader;
        GLuint shaders[FV_N_ELEMENTS(fv_shader_data_shaders)];
        GLuint program;
        bool result = true;
        int n_shaders;
        int i, j;

        for (n_shaders = 0; n_shaders < FV_N_ELEMENTS(shaders); n_shaders++) {
                shader = fv_shader_data_shaders + n_shaders;
                shaders[n_shaders] =
                        create_shader_from_files(shader->type,
                                                 shader->filenames);
                if (shaders[n_shaders] == 0) {
                        result = false;
                        goto out;
                }
        }

        for (i = 0; i < FV_SHADER_DATA_N_PROGRAMS; i++)
                data->programs[i] = fv_gl.glCreateProgram();

        for (i = 0; i < FV_N_ELEMENTS(shaders); i++) {
                shader = fv_shader_data_shaders + i;
                for (j = 0; shader->programs[j] != PROGRAMS_END; j++) {
                        program = data->programs[shader->programs[j]];
                        fv_gl.glAttachShader(program, shaders[i]);
                }
        }

        if (!link_programs(data)) {
                for (i = 0; i < FV_SHADER_DATA_N_PROGRAMS; i++)
                        fv_gl.glDeleteProgram(data->programs[i]);
                result = false;
        }

out:
        for (i = 0; i < n_shaders; i++)
                fv_gl.glDeleteShader(shaders[i]);

        return result;
}

void
fv_shader_data_destroy(struct fv_shader_data *data)
{
        int i;

        for (i = 0; i < FV_SHADER_DATA_N_PROGRAMS; i++)
                fv_gl.glDeleteProgram(data->programs[i]);
}
