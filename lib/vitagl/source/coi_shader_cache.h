#ifndef COI_SHADER_CACHE_H
#define COI_SHADER_CACHE_H
#include <stdint.h>

/* Read on the GL thread. Cache timings are a subset of glLinkProgram time. */
typedef struct {
    uint64_t io_us, lookup_us, restore_us;
    uint32_t hits, misses, ram_hits;
} CoiShaderCacheStats;
void vgl_coi_shader_cache_stats(CoiShaderCacheStats *stats);
void vgl_coi_shader_cache_flush(void);
#endif
