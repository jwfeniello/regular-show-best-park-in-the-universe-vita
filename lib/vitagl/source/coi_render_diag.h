#ifndef COI_RENDER_DIAG_H
#define COI_RENDER_DIAG_H
#include <stdint.h>

/* Render-thread-only observations; these do not change any GL/GXM state. */
void vgl_coi_trace_draw(uint32_t mode, int32_t count, uint32_t index_type);
void vgl_coi_trace_frame(void);
#endif
