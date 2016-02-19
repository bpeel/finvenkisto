/*
 * Finvenkisto
 *
 * Copyright (C) 2014, 2016 Neil Roberts
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

#include <GL/gl.h>
#include <dlfcn.h>

#include "fv-gl.h"
#include "fv-util.h"
#include "fv-buffer.h"
#include "fv-error-message.h"

struct fv_gl fv_gl;

struct fv_gl_func {
        const char *name;
        size_t offset;
};

struct fv_gl_group {
        int minimum_gl_version;
        const char *extension;
        const char *extension_suffix;
        const struct fv_gl_func *funcs;
};

static const struct fv_gl_group
gl_groups[] = {
#define FV_GL_BEGIN_GROUP(min_gl_version, ext, suffix)  \
        { .minimum_gl_version = min_gl_version,         \
        .extension = ext,                               \
        .extension_suffix = suffix,                     \
        .funcs = (const struct fv_gl_func[]) {
#define FV_GL_FUNC(return_type, func_name, args)                        \
        { .name = #func_name, .offset = offsetof(struct fv_gl, func_name) },
#define FV_GL_END_GROUP()                       \
        { .name = NULL }                        \
} },
#include "fv-gl-funcs.h"
#undef FV_GL_BEGIN_GROUP
#undef FV_GL_FUNC
#undef FV_GL_END_GROUP
};

static bool
check_extension_in_list(const char *haystack,
                        const char *needle)
{
        int needle_len = strlen (needle);
        const char *haystack_end;
        const char *end;

        haystack_end = haystack + strlen (haystack);

        while (haystack < haystack_end) {
                end = strchr(haystack, ' ');

                if (end == NULL)
                        end = haystack_end;

                if (end - haystack == needle_len &&
                    !memcmp (haystack, needle, needle_len))
                        return true;

                haystack = end + 1;
        }

        return false;
}

static bool
extension_supported(const char *extension)
{
        const char *extensions, *this_extension;
        GLint n_extensions, i;

        if (fv_gl.major_version < 3) {
                extensions = (const char *) fv_gl.glGetString(GL_EXTENSIONS);

                return check_extension_in_list(extensions, extension);
        }

        fv_gl.glGetIntegerv(GL_NUM_EXTENSIONS, &n_extensions);

        for (i = 0; i < n_extensions; i++) {
                this_extension =
                        (const char *) fv_gl.glGetStringi(GL_EXTENSIONS, i);
                if (!strcmp(extension, this_extension))
                        return true;
        }

        return false;
}

static void
get_gl_version(void)
{
        const char *version_string =
                (const char *) fv_gl.glGetString(GL_VERSION);
        const char *number_start, *p;
        int major_version = 0;
        int minor_version = 0;

        number_start = p = version_string;

        while (*p >= '0' && *p <= '9') {
                major_version = major_version * 10 + *p - '0';
                p++;
        }

        if (p == number_start || *p != '.')
                goto invalid;

        p++;

        number_start = p;

        while (*p >= '0' && *p <= '9') {
                minor_version = minor_version * 10 + *p - '0';
                p++;
        }

        if (number_start == p)
                goto invalid;

        fv_gl.major_version = major_version;
        fv_gl.minor_version = minor_version;

        return;

invalid:
        fv_gl.major_version = -1;
        fv_gl.minor_version = -1;
}

static void
init_group(const struct fv_gl_group *group)
{
        int minor_gl_version = fv_gl.minor_version;
        const char *suffix;
        struct fv_buffer buffer;
        void *func;
        int gl_version;
        int i;

        if (minor_gl_version >= 10)
                minor_gl_version = 9;
        gl_version = fv_gl.major_version * 10 + minor_gl_version;

        if (group->minimum_gl_version >= 0 &&
            gl_version >= group->minimum_gl_version)
                suffix = "";
        else if (group->extension && extension_supported(group->extension))
                suffix = group->extension_suffix;
        else
                return;

        fv_buffer_init(&buffer);

        for (i = 0; group->funcs[i].name; i++) {
                fv_buffer_set_length(&buffer, 0);
                fv_buffer_append_string(&buffer, group->funcs[i].name);
                fv_buffer_append_string(&buffer, suffix);
                func = fv_gl.glXGetProcAddress((char *) buffer.data);
                *(void **) ((char *) &fv_gl + group->funcs[i].offset) = func;
        }

        fv_buffer_destroy(&buffer);
}

void
fv_gl_init(void)
{
        int sample_buffers = 0;
        int i;

        fv_gl.glGetString = fv_gl.glXGetProcAddress("glGetString");

        get_gl_version();

        for (i = 0; i < FV_N_ELEMENTS(gl_groups); i++)
                init_group(gl_groups + i);

        fv_gl.have_map_buffer_range = fv_gl.glMapBufferRange != NULL;
        fv_gl.have_vertex_array_objects = fv_gl.glGenVertexArrays != NULL;

        fv_gl.have_npot_mipmaps = true;

        fv_gl.have_texture_2d_array =
                extension_supported("GL_EXT_texture_array");

        fv_gl.have_instanced_arrays =
                fv_gl.glVertexAttribDivisor != NULL &&
                fv_gl.glDrawElementsInstanced != NULL;

        fv_gl.glGetIntegerv(GL_SAMPLE_BUFFERS_ARB, &sample_buffers);

        fv_gl.have_multisampling = sample_buffers != 0;
}

bool
fv_gl_init_glx(Display *display)
{
        int glx_major, glx_minor;
        const char *extensions;
        int i;
        static const char *required_extensions[] = {
                "GLX_ARB_create_context",
                "GLX_ARB_get_proc_address"
        };

        memset(&fv_gl, 0, sizeof fv_gl);

        fv_gl.lib_gl = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);

        if (fv_gl.lib_gl == NULL) {
                fv_error_message("Error openining libGL.so.1: %s",
                                 dlerror());
                return false;
        }

        fv_gl.glXDestroyContext =
                dlsym(fv_gl.lib_gl, "glXDestroyContext");
        fv_gl.glXQueryExtensionsString =
                dlsym(fv_gl.lib_gl, "glXQueryExtensionsString");
        fv_gl.glXQueryVersion =
                dlsym(fv_gl.lib_gl, "glXQueryVersion");
        fv_gl.glXSwapBuffers =
                dlsym(fv_gl.lib_gl, "glXSwapBuffers");

        fv_gl.glXQueryVersion(display, &glx_major, &glx_minor);

        if (glx_major < 1 || (glx_major == 1 && glx_minor < 3)) {
                fv_error_message("GLX 1.3 support is required but only "
                                 "%i.%i was found",
                                 glx_major, glx_minor);
                goto error;
        }

        extensions = fv_gl.glXQueryExtensionsString(display,
                                                    DefaultScreen(display));

        for (i = 0; i < FV_N_ELEMENTS(required_extensions); i++) {
                if (!check_extension_in_list(extensions,
                                             required_extensions[i])) {
                        fv_error_message("%s extension is not supported",
                                         required_extensions[i]);
                        goto error;
                }
        }

        fv_gl.glXGetProcAddress = dlsym(fv_gl.lib_gl, "glXGetProcAddressARB");

        fv_gl.glXChooseFBConfig =
                fv_gl.glXGetProcAddress("glXChooseFBConfig");
        fv_gl.glXCreateContextAttribs =
                fv_gl.glXGetProcAddress("glXCreateContextAttribsARB");
        fv_gl.glXCreateWindow =
                fv_gl.glXGetProcAddress("glXCreateWindow");
        fv_gl.glXDestroyWindow =
                fv_gl.glXGetProcAddress("glXDestroyWindow");
        fv_gl.glXGetVisualFromFBConfig =
                fv_gl.glXGetProcAddress("glXGetVisualFromFBConfig");
        fv_gl.glXMakeContextCurrent =
                fv_gl.glXGetProcAddress("glXMakeContextCurrent");

        return true;

error:
        dlclose(fv_gl.lib_gl);

        return false;
}

void
fv_gl_deinit_glx(void)
{
        dlclose(fv_gl.lib_gl);

        memset(&fv_gl, 0, sizeof fv_gl);
}
