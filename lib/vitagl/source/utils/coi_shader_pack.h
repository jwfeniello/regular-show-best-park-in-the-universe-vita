#ifndef COI_SHADER_PACK_H
#define COI_SHADER_PACK_H
#include <stdbool.h>
#include <stdint.h>

#define COI_PACK_LIMIT (16u * 1024u * 1024u)
#define COI_PACK_MAX_RECORDS 8192u
#define COI_PACK_KEY_SIZE 40u
#define COI_PACK_RECORD_SIZE 48u

/* CPU-side serialized shaders, separate from vitaGL's GPU allocations.
   Records: name[40], uint32 length, uint32 reserved, original .gxp bytes,
   then zero padding to an eight-byte boundary. The file starts "COIPACK1".
   Index offsets remain valid if the backing allocation moves. */
typedef struct {
    uint8_t *data;
    uint32_t used, capacity;
    uint32_t *index;
    uint32_t count, index_capacity, records;
    bool dirty;
} CoiShaderPack;

void coi_pack_clear(CoiShaderPack *pack);
/* Adopts data only on success; the caller retains ownership on failure. */
bool coi_pack_adopt(CoiShaderPack *pack, uint8_t *data, uint32_t size);
const void *coi_pack_get(const CoiShaderPack *pack, const char *key, uint32_t *size);
/* Input must not point into pack->data. A put can invalidate borrowed pointers. */
bool coi_pack_put(CoiShaderPack *pack, const char *key, const void *data, uint32_t size);
#endif
