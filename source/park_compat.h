#ifndef SCRIB_COMPAT_H
#define SCRIB_COMPAT_H
#include <vitaGL.h>
#include <stdint.h>
int park_uname(void *out);
int park_sigprocmask(int how, const uint32_t *set, uint32_t *old);
void park_exit(int status);
void park_bind_renderbuffer(GLenum target, GLuint name);
void park_renderbuffer_storage(GLenum target, GLenum format, GLsizei width, GLsizei height);
void park_delete_renderbuffers(GLsizei count, const GLuint *names);
void park_get_renderbuffer_parameter(GLenum target, GLenum pname, GLint *value);
#endif
