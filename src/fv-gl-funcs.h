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

/* This header is included multiple times with different definitions
 * of the FV_GL_FUNC macro
 */

FV_GL_FUNC(void,
           glAttachShader, (GLuint program, GLuint shader))
FV_GL_FUNC(void,
           glBindBuffer, (GLenum target, GLuint buffer))
FV_GL_FUNC(GLboolean,
           glUnmapBuffer, (GLenum target))
FV_GL_FUNC(void *,
           glMapBufferRange, (GLenum target, GLintptr offset,
                              GLsizeiptr length, GLbitfield access))
FV_GL_FUNC(void,
           glFlushMappedBufferRange, (GLenum target, GLintptr offset,
                                      GLsizei length))
FV_GL_FUNC(void,
           glBindTexture, (GLenum target, GLuint texture))
FV_GL_FUNC(void,
           glBindVertexArray, (GLuint array))
FV_GL_FUNC(void,
           glBufferData, (GLenum target, GLsizeiptr size,
                          const void *data, GLenum usage))
FV_GL_FUNC(void,
           glBufferSubData, (GLenum target, GLintptr offset,
                             GLsizeiptr size, const void *data))
FV_GL_FUNC(void,
           glClear, (GLbitfield mask))
FV_GL_FUNC(void,
           glCompileShader, (GLuint shader))
FV_GL_FUNC(GLuint,
           glCreateProgram, (void))
FV_GL_FUNC(GLuint,
           glCreateShader, (GLenum type))
FV_GL_FUNC(void,
           glDeleteBuffers, (GLsizei n, const GLuint *buffers))
FV_GL_FUNC(void,
           glDeleteProgram, (GLuint program))
FV_GL_FUNC(void,
           glDeleteShader, (GLuint shader))
FV_GL_FUNC(void,
           glDeleteTextures, (GLsizei n, const GLuint *textures))
FV_GL_FUNC(void,
           glDeleteVertexArrays, (GLsizei n, const GLuint *arrays))
FV_GL_FUNC(void,
           glDisable, (GLenum cap))
FV_GL_FUNC(void,
           glDrawRangeElements, (GLenum mode, GLuint start,
                                 GLuint end, GLsizei count, GLenum type,
                                 const GLvoid *indices))
FV_GL_FUNC(void,
           glDrawElementsInstanced, (GLenum mode, GLsizei count, GLenum type,
                                     const void *indices,
                                     GLsizei instancecount))
FV_GL_FUNC(void,
           glEnable, (GLenum cap))
FV_GL_FUNC(void,
           glEnableVertexAttribArray, (GLuint index))
FV_GL_FUNC(void,
           glGenBuffers, (GLsizei n, GLuint *buffers))
FV_GL_FUNC(void,
           glGenerateMipmap, (GLenum target))
FV_GL_FUNC(void,
           glGenTextures, (GLsizei n, GLuint *textures))
FV_GL_FUNC(void,
           glGenVertexArrays, (GLsizei n, GLuint *arrays))
FV_GL_FUNC(void,
           glGetProgramInfoLog, (GLuint program, GLsizei bufSize,
                                 GLsizei *length, GLchar *infoLog))
FV_GL_FUNC(void,
           glGetProgramiv, (GLuint program, GLenum pname, GLint *params))
FV_GL_FUNC(void,
           glGetShaderInfoLog, (GLuint shader, GLsizei bufSize,
                                GLsizei *length, GLchar *infoLog))
FV_GL_FUNC(void,
           glGetShaderiv, (GLuint shader, GLenum pname, GLint *params))
FV_GL_FUNC(GLint,
           glGetUniformLocation, (GLuint program, const GLchar *name))
FV_GL_FUNC(void,
           glLinkProgram, (GLuint program))
FV_GL_FUNC(void,
           glShaderSource, (GLuint shader, GLsizei count,
                            const GLchar *const*string, const GLint *length))
FV_GL_FUNC(void,
           glTexImage2D, (GLenum target, GLint level,
                          GLint internalFormat,
                          GLsizei width, GLsizei height,
                          GLint border, GLenum format, GLenum type,
                          const GLvoid *pixels))
FV_GL_FUNC(void,
           glTexImage3D, (GLenum target, GLint level,
                          GLint internalFormat,
                          GLsizei width, GLsizei height,
                          GLsizei depth, GLint border,
                          GLenum format, GLenum type,
                          const GLvoid *pixels ))
FV_GL_FUNC(void,
           glTexSubImage3D, (GLenum target, GLint level,
                             GLint xoffset, GLint yoffset,
                             GLint zoffset, GLsizei width,
                             GLsizei height, GLsizei depth,
                             GLenum format,
                             GLenum type, const GLvoid *pixels))
FV_GL_FUNC(void,
           glTexParameteri, (GLenum target, GLenum pname, GLint param))
FV_GL_FUNC(void,
           glUniform1i, (GLint location, GLint v0))
FV_GL_FUNC(void,
           glUniformMatrix4fv, (GLint location, GLsizei count,
                                GLboolean transpose, const GLfloat *value))
FV_GL_FUNC(void,
           glUseProgram, (GLuint program))
FV_GL_FUNC(void,
           glVertexAttribPointer, (GLuint index, GLint size,
                                   GLenum type, GLboolean normalized,
                                   GLsizei stride, const void *pointer))
FV_GL_FUNC(void,
           glVertexAttribDivisor, (GLuint index, GLuint divisor))
FV_GL_FUNC(void,
           glViewport, (GLint x, GLint y,
                               GLsizei width, GLsizei height))
