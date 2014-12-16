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

static const char
fv_shader_data_version[] =
        "#version 330 core\n"
        "\n";

struct fv_shader_data_shader {
        GLenum type;
        const char *filename;
        enum fv_shader_data_program programs[FV_SHADER_DATA_N_PROGRAMS + 1];
};

static const struct fv_shader_data_shader
fv_shader_data_shaders[] = {
        {
                GL_FRAGMENT_SHADER,
                "fv-texture-fragment.glsl",
                { FV_SHADER_DATA_PROGRAM_TEXTURE, -1 }
        },
        {
                GL_VERTEX_SHADER,
                "fv-texture-vertex.glsl",
                { FV_SHADER_DATA_PROGRAM_TEXTURE, -1 }
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
        const char *source_strings[2];
        GLint lengths[2];

        shader = fv_gl.glCreateShader(type);

        source_strings[0] = fv_shader_data_version;
        lengths[0] = sizeof fv_shader_data_version - 1;
        source_strings[1] = source;
        lengths[1] = source_length;
        fv_gl.glShaderSource(shader,
                             FV_N_ELEMENTS(source_strings),
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
                fprintf(stderr, "%s compilation failed\n", name);
                fv_gl.glDeleteShader(shader);
                return 0;
        }

        return shader;
}

static GLuint
create_shader_from_file(GLenum shader_type,
                        const char *filename)
{
        char *fullname;
        FILE *file;
        char *source;
        long int length;
        size_t got;
        GLuint shader;

        fullname = fv_data_get_filename(filename);

        if (fullname == NULL) {
                fprintf(stderr, "Couldn't get filename for %s\n", filename);
                return 0;
        }

        file = fopen(fullname, "r");

        fv_free(fullname);

        if (file == NULL) {
                fprintf(stderr, "%s: %s\n", filename, strerror(errno));
                return 0;
        }

        if (fseek(file, 0, SEEK_END) != 0)
                goto file_error;

        length = ftell(file);
        if (length == -1)
                goto file_error;

        if (fseek(file, 0, SEEK_SET) != 0)
                goto file_error;

        source = fv_alloc(length);

        got = fread(source, 1, length, file);

        if (got != length) {
                fv_free(source);
                if (ferror(file))
                        goto file_error;
                fprintf(stderr, "%s: Unexpected EOF\n", filename);
                goto close_error;
        }

        fclose(file);

        shader = create_shader(filename, shader_type, source, length);

        fv_free(source);

        return shader;

file_error:
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
close_error:
        fclose(file);

        return 0;
}

static bool
shader_contains_program(const struct fv_shader_data_shader *shader,
                        enum fv_shader_data_program program_num)
{
        int i;

        for (i = 0; shader->programs[i] != -1; i++) {
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
        int i;

        /* Generate the program name as just a list of the shaders it
         * contains */

        fv_buffer_append_c(&buffer, '(');

        for (i = 0; i < FV_N_ELEMENTS(fv_shader_data_shaders); i++) {
                shader = fv_shader_data_shaders + i;

                if (shader_contains_program(shader, program_num)) {
                        if (buffer.length > 1)
                                fv_buffer_append_string(&buffer, ", ");
                        fv_buffer_append_string(&buffer, shader->filename);
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
                fprintf(stderr, "%s program link failed\n", program_name);
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
                        create_shader_from_file(shader->type,
                                                shader->filename);
                if (shaders[n_shaders] == 0) {
                        result = false;
                        goto out;
                }
        }

        for (i = 0; i < FV_SHADER_DATA_N_PROGRAMS; i++)
                data->programs[i] = fv_gl.glCreateProgram();

        for (i = 0; i < FV_N_ELEMENTS(shaders); i++) {
                shader = fv_shader_data_shaders + i;
                for (j = 0; shader->programs[j] != -1; j++) {
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
