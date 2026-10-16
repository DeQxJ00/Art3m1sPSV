#include "psb.h"

uint32_t em_psb_resource_index(const uint8_t *p) {
    uint32_t x=0;
    if(*p>=25 && *p<=28)for(unsigned i=0;i<(unsigned)(*p-24);++i)x|=(uint32_t)p[i+1]<<(i*8);
    return x;
}
const uint8_t *em_psb_resource_at(const EmPsb *p,uint32_t i){
    if(!p->sections[6])return NULL;
    return p->sections[6]+em_psb_array_get(em_psb_array(p->sections[4]),i);
}
uint32_t em_psb_resource_size_at(const EmPsb *p,uint32_t i){
    return em_psb_array_get(em_psb_array(p->sections[5]),i);
}
const uint8_t *em_psb_resource(const EmPsbValue *v){return em_psb_resource_at(v->psb,em_psb_resource_index(v->data));}
uint32_t em_psb_resource_size(const EmPsbValue *v){return em_psb_resource_size_at(v->psb,em_psb_resource_index(v->data));}
void em_psb_object_at(EmPsbValue *out,const EmPsbValue *v,uint32_t i){
    EmPsbArray keys=em_psb_array(v->data+1), offsets=em_psb_array(keys.end);
    *out=(EmPsbValue){v->psb,offsets.end+em_psb_array_get(offsets,i)};
}
int em_psb_object_key(const EmPsbValue *v,uint32_t i,char *out,size_t cap){
    EmPsbArray keys=em_psb_array(v->data+1),base=em_psb_array(v->psb->sections[1]);
    EmPsbArray check=em_psb_array(base.end),ends=em_psb_array(check.end);
    uint32_t s=em_psb_array_get(check,em_psb_array_get(ends,em_psb_array_get(keys,i)));size_t n=0;
    if(!cap)return 0;
    while(s){if(n+1>=cap){out[0]=0;return 0;}uint32_t parent=em_psb_array_get(check,s);out[n++]=(char)(s-em_psb_array_get(base,parent));s=parent;}
    out[n]=0;for(size_t j=0;j<n/2;++j){char c=out[j];out[j]=out[n-j-1];out[n-j-1]=c;}return 1;
}

static uint32_t little(const uint8_t *p, uint32_t n) {
    uint32_t x = 0;
    for (uint32_t i = 0; i < n; ++i) x |= (uint32_t)p[i] << (i * 8);
    return x;
}
EmPsbArray em_psb_array(const uint8_t *p) {
    uint32_t n = p[0] - 12;
    EmPsbArray a;
    a.count = little(p + 1, n);
    a.width = p[n + 1] - 12;
    a.data = p + n + 2;
    a.end = a.data + a.count * a.width;
    return a;
}
uint32_t em_psb_array_get(EmPsbArray a, uint32_t index) {
    return little(a.data + index * a.width, a.width);
}
EmPsb *em_psb_init_header(EmPsb *psb) {
    psb->version = little(psb->raw + 4, 4);
    for (uint32_t i = 0; i < 8; ++i)
        psb->sections[i] = psb->raw + little(psb->raw + 8 + i * 4, 4);
    psb->resource_bytes = psb->length - little(psb->raw + 32, 4);
    return psb;
}
void em_psb_root(EmPsbValue *out, const EmPsb *psb) {
    out->psb = psb;
    out->data = psb->sections[7];
}
int em_psb_name_id(const EmPsb *psb, const uint8_t *name, uint32_t *out) {
    /* v1 has a separate strcmp/binary-search representation. Not recovered here. */
    if (psb->version == 1) return 0;
    EmPsbArray base = em_psb_array(psb->sections[1]);
    EmPsbArray check = em_psb_array(base.end);
    uint32_t state = 0;
    for (;;) {
        uint32_t next = em_psb_array_get(base, state) + *name;
        if (next >= base.count || em_psb_array_get(check, next) != state) return 0;
        if (!*name) { *out = em_psb_array_get(base, next); return 1; }
        state = next;
        ++name;
    }
}
int em_psb_find(const EmPsbValue *value, const uint8_t *name, EmPsbValue *out) {
    uint32_t id;
    if (!em_psb_name_id(value->psb, name, &id)) return 0;
    EmPsbArray keys = em_psb_array(value->data + 1);
    uint32_t lo = 0, hi = keys.count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) >> 1;
        uint32_t key = em_psb_array_get(keys, mid);
        if (key == id) {
            EmPsbArray offsets = em_psb_array(keys.end);
            out->psb = value->psb;
            out->data = offsets.end + em_psb_array_get(offsets, mid);
            return 1;
        }
        if (key < id) lo = mid + 1; else hi = mid;
    }
    return 0;
}
void em_psb_at(EmPsbValue *out, const EmPsbValue *value, uint32_t index) {
    EmPsbArray offsets = em_psb_array(value->data + 1);
    out->psb = value->psb;
    out->data = offsets.end + em_psb_array_get(offsets, index);
}
uint32_t em_psb_key_number(const uint8_t *p) {
    return *p >= 17 && *p <= 20 ? little(p + 1, *p - 16) : 0;
}
int32_t em_psb_read_i32(const uint8_t *p) {
    if (*p < 5 || *p > 8) return 0;
    uint32_t n = *p - 4, x = little(p + 1, n);
    if (n < 4 && (p[n] & 128)) x |= UINT32_MAX << (n * 8);
    return (int32_t)x;
}
int64_t em_psb_read_i64(const uint8_t *p) {
    if (*p < 9 || *p > 12) return 0;
    uint32_t n = *p - 4;
    uint64_t x = (uint64_t)little(p + 1, 4) | (uint64_t)little(p + 5, n - 4) << 32;
    if (n < 8 && (p[n] & 128)) x |= UINT64_MAX << (n * 8);
    return (int64_t)x;
}
double em_psb_read_double(const uint8_t *p) {
    union { uint64_t bits; double number; } v;
    v.bits = *p == 31 ? (uint64_t)little(p + 1, 4) | (uint64_t)little(p + 5, 4) << 32 : 0;
    return v.number;
}
static float read_float(const uint8_t *p) {
    union { uint32_t bits; float number; } v;
    v.bits = *p == 30 ? little(p + 1, 4) : 0;
    return v.number;
}
static double original_i64_double(int64_t x) {
    /* The original builds magnitude from two U32 words in D16/D18, then
       applies sign. Preserve double rounding before its final float cast. */
    uint64_t magnitude = x < 0 ? 0 - (uint64_t)x : (uint64_t)x;
    double d = (double)(uint32_t)(magnitude >> 32) * 4294967296.0;
    d = d + (double)(uint32_t)magnitude;
    return d * (x < 0 ? -1.0 : 1.0);
}
int32_t em_psb_int(const EmPsbValue *value) {
    const uint8_t *p = value->data;
    if (*p == 2 || *p == 3) return *p == 2;
    if (*p >= 4 && *p <= 8) return em_psb_read_i32(p);
    if (*p >= 9 && *p <= 12) return (int32_t)em_psb_read_i64(p);
    /* Match ARM VCVT.S32: truncation, saturation, NaN -> 0. */
    double d = *p == 31 ? em_psb_read_double(p) : (double)read_float(p);
    if (d != d) return 0;
    if (d >= 2147483647.0) return INT32_MAX;
    if (d <= -2147483648.0) return INT32_MIN;
    return (int32_t)d;
}
float em_psb_float(const EmPsbValue *value) {
    const uint8_t *p = value->data;
    if (*p == 2 || *p == 3) return (float)(*p == 2);
    if (*p >= 4 && *p <= 8) return (float)em_psb_read_i32(p);
    if (*p >= 9 && *p <= 12) return (float)original_i64_double(em_psb_read_i64(p));
    if (*p == 31) return (float)em_psb_read_double(p);
    return read_float(p);
}
int em_psb_bool(const EmPsbValue *value) {
    const uint8_t *p = value->data;
    if (*p == 2 || *p == 3) return *p == 2;
    if (*p >= 4 && *p <= 8) return em_psb_read_i32(p) != 0;
    if (*p >= 9 && *p <= 12) return em_psb_read_i64(p) != 0;
    if (*p == 31) return em_psb_read_double(p) != 0;
    return read_float(p) != 0;
}
const char *em_psb_string_at(const EmPsb *psb, uint32_t index) {
    return (const char *)psb->sections[3] + em_psb_array_get(em_psb_array(psb->sections[2]), index);
}
const char *em_psb_string(const EmPsbValue *value) {
    const uint8_t *p = value->data;
    uint32_t index = *p >= 21 && *p <= 24 ? little(p + 1, *p - 20) : 0;
    return em_psb_string_at(value->psb, index);
}
