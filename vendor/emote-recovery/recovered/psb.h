/* Reconstructed from PCSG00341 ARM code, not from a third-party PSB reader.
 * This layer mirrors the original unchecked accessors for valid decoded PSB
 * v2/v3 input. It is not a validator for untrusted files. See PSB_RECOVERY.md.
 */
#ifndef RECOVERED_PSB_H
#define RECOVERED_PSB_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t vtable_tag, reserved1, reserved2;
    const uint8_t *raw;
    uint32_t length, mode, version, reserved7;
    const uint8_t *sections[8];
    uint32_t resource_bytes, reserved17;
} EmPsb;
typedef struct { const EmPsb *psb; const uint8_t *data; } EmPsbValue;
typedef struct {
    uint32_t count, width;
    const uint8_t *data, *end;
} EmPsbArray;
_Static_assert(sizeof(void *) == 4, "original PSV pointer ABI");
_Static_assert(sizeof(EmPsb) == 72, "original PSB layout");
_Static_assert(offsetof(EmPsb, sections) == 32, "original PSB fields");

EmPsb *em_psb_init_header(EmPsb *psb); /* 811A3970 */
void em_psb_root(EmPsbValue *out, const EmPsb *psb); /* 811A3968 */
int em_psb_name_id(const EmPsb *psb, const uint8_t *name, uint32_t *out); /* 811A1F5C, v2+ branch */
int em_psb_find(const EmPsbValue *value, const uint8_t *name, EmPsbValue *out); /* 811A2436, v2+ branch */
void em_psb_at(EmPsbValue *out, const EmPsbValue *value, uint32_t index); /* 811A342C */
uint32_t em_psb_key_number(const uint8_t *p); /* 811A23E4 */
int32_t em_psb_read_i32(const uint8_t *p); /* 811A3556 */
int64_t em_psb_read_i64(const uint8_t *p); /* 811A35AA */
double em_psb_read_double(const uint8_t *p); /* 811A3670 */
int32_t em_psb_int(const EmPsbValue *value); /* 811A36BA */
float em_psb_float(const EmPsbValue *value); /* 811A376C */
int em_psb_bool(const EmPsbValue *value); /* 811A3880 */
const char *em_psb_string_at(const EmPsb *psb, uint32_t index); /* 811A2990 */
const char *em_psb_string(const EmPsbValue *value); /* 811A2A42 */

/* Factored helpers from repeated inline array code in the original functions. */
EmPsbArray em_psb_array(const uint8_t *p);
uint32_t em_psb_array_get(EmPsbArray a, uint32_t index);
uint32_t em_psb_resource_index(const uint8_t *p); /* 811A3BA8 */
const uint8_t *em_psb_resource_at(const EmPsb *p, uint32_t index); /* 811A3AE6 */
uint32_t em_psb_resource_size_at(const EmPsb *p, uint32_t index); /* 811A3C12 */
const uint8_t *em_psb_resource(const EmPsbValue *v); /* 811A3BFA */
uint32_t em_psb_resource_size(const EmPsbValue *v); /* 811A3CBE */
/* Bounded host helpers using the original name trie and object layout. */
void em_psb_object_at(EmPsbValue *out,const EmPsbValue *v,uint32_t index);
int em_psb_object_key(const EmPsbValue *v,uint32_t index,char *out,size_t capacity);
#endif
