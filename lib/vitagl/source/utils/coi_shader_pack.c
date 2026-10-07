#include "coi_shader_pack.h"
#include <stdlib.h>
#include <string.h>

static uint32_t read32(const void *p) {
    uint32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}
static bool hex_chars(const char *s, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'A' && s[i] <= 'F') ||
              (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}
static bool valid_key(const char *s) {
    unsigned n = 0;
    while (n < COI_PACK_KEY_SIZE && s[n]) ++n;
    if (n == 39)
        return hex_chars(s, 16) && s[16] == '-' && hex_chars(s + 17, 16) &&
            s[33] == '-' && (s[34] == 'v' || s[34] == 'f') && !strcmp(s + 35, ".gxp");
    return n >= 5 && n <= 20 && hex_chars(s, n - 4) && !strcmp(s + n - 4, ".gxp");
}
static uint32_t lower_bound(const CoiShaderPack *p, const char *key, bool *found) {
    uint32_t lo = 0, hi = p->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (strcmp((char *)p->data + p->index[mid], key) < 0) lo = mid + 1;
        else hi = mid;
    }
    *found = lo < p->count && !strcmp((char *)p->data + p->index[lo], key);
    return lo;
}
static bool reserve_index(CoiShaderPack *p) {
    if (p->count < p->index_capacity) return true;
    uint32_t capacity = p->index_capacity ? p->index_capacity * 2 : 128;
    if (capacity > COI_PACK_MAX_RECORDS) capacity = COI_PACK_MAX_RECORDS;
    if (capacity <= p->count) return false;
    uint32_t *index = realloc(p->index, capacity * sizeof(*index));
    if (!index) return false;
    p->index = index; p->index_capacity = capacity;
    return true;
}
static bool index_record(CoiShaderPack *p, uint32_t offset) {
    bool found;
    uint32_t at = lower_bound(p, (char *)p->data + offset, &found);
    if (!found) {
        if (!reserve_index(p)) return false;
        memmove(p->index + at + 1, p->index + at, (p->count - at) * sizeof(*p->index));
        ++p->count;
    }
    p->index[at] = offset; /* A replacement record supersedes the older one. */
    ++p->records;
    return true;
}
void coi_pack_clear(CoiShaderPack *p) {
    free(p->data); free(p->index);
    memset(p, 0, sizeof(*p));
}
bool coi_pack_adopt(CoiShaderPack *p, uint8_t *data, uint32_t size) {
    if (!data || size < 8 || size > COI_PACK_LIMIT || memcmp(data, "COIPACK1", 8)) return false;
    CoiShaderPack next = {.data = data, .used = size, .capacity = size};
    uint32_t offset = 8;
    while (offset < size) {
        if (size - offset < COI_PACK_RECORD_SIZE || next.records == COI_PACK_MAX_RECORDS) goto bad;
        const uint8_t *record = data + offset;
        if (!memchr(record, 0, COI_PACK_KEY_SIZE) || !valid_key((char *)record) || read32(record + 44)) goto bad;
        uint32_t bytes = read32(record + 40);
        if (bytes < 80 || bytes > 4u * 1024u * 1024u + 16u) goto bad;
        uint32_t stride = COI_PACK_RECORD_SIZE + ((bytes + 7u) & ~7u);
        if (stride > size - offset || !index_record(&next, offset)) goto bad;
        offset += stride;
    }
    coi_pack_clear(p);
    *p = next;
    return true;
bad:
    free(next.index);
    return false;
}
const void *coi_pack_get(const CoiShaderPack *p, const char *key, uint32_t *size) {
    if (!p->data || !valid_key(key)) return NULL;
    bool found;
    uint32_t at = lower_bound(p, key, &found);
    if (!found) return NULL;
    const uint8_t *record = p->data + p->index[at];
    *size = read32(record + 40);
    return record + COI_PACK_RECORD_SIZE;
}
bool coi_pack_put(CoiShaderPack *p, const char *key, const void *data, uint32_t size) {
    if (!data || !valid_key(key) || size < 80 || size > 4u * 1024u * 1024u + 16u ||
        p->records >= COI_PACK_MAX_RECORDS) return false;
    uint32_t used = p->data ? p->used : 8;
    uint32_t stride = COI_PACK_RECORD_SIZE + ((size + 7u) & ~7u);
    if (stride > COI_PACK_LIMIT - used || !reserve_index(p)) return false;
    uint32_t needed = used + stride;
    if (needed > p->capacity) {
        uint32_t capacity = p->capacity > 65536 ? p->capacity : 65536;
        while (capacity < needed) {
            capacity = capacity > COI_PACK_LIMIT / 2 ? COI_PACK_LIMIT : capacity * 2;
        }
        uint8_t *buffer = realloc(p->data, capacity);
        if (!buffer) return false;
        p->data = buffer; p->capacity = capacity;
    }
    if (!p->used) memcpy(p->data, "COIPACK1", 8);
    uint8_t *record = p->data + used;
    memset(record, 0, stride);
    memcpy(record, key, strlen(key) + 1);
    memcpy(record + 40, &size, sizeof(size));
    memcpy(record + COI_PACK_RECORD_SIZE, data, size);
    /* Index storage was reserved above, so this cannot allocate or fail. */
    if (!index_record(p, used)) return false;
    p->used = needed; p->dirty = true;
    return true;
}
