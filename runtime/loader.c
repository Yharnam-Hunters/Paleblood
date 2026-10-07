/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Paleblood's PS4 executable loader (runtime/include/runtime/loader.h). Written from the
   published ELF and SCE dynamic-linking formats, not from another loader's code. */
#define _GNU_SOURCE
#include "runtime/loader.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_TLS 7
#define PT_SCE_DYNLIBDATA 0x61000000u
#define PT_SCE_PROCPARAM 0x61000001u
#define DT_NULL 0
#define DT_SCE_EXPORT_LIB 0x61000013
#define DT_SCE_IMPORT_LIB 0x61000015
#define DT_SCE_JMPREL 0x61000029
#define DT_SCE_PLTRELSZ 0x6100002d
#define DT_SCE_RELA 0x6100002f
#define DT_SCE_RELASZ 0x61000031
#define DT_SCE_STRTAB 0x61000035
#define DT_SCE_STRSZ 0x61000037
#define DT_SCE_SYMTAB 0x61000039
#define DT_SCE_SYMTABSZ 0x6100003f
#define R_X86_64_64 1
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE 8
#define R_X86_64_DTPMOD64 16
#define STT_OBJECT 1

enum { KIND_LOCAL = 0, KIND_IMPORT = 1, KIND_TLS_MODULE = 2 };

typedef struct {
    uint64_t target;   /* offset in the image */
    uint32_t kind;
    uint64_t value;    /* KIND_LOCAL: offset in the image; KIND_IMPORT: import index */
    int64_t addend;    /* added to an import's bound address */
} reloc;

typedef struct {
    char *name;
    int kind;
} import;

struct rt_elf {
    unsigned char *file;
    size_t file_size;
    uint64_t entry, image_size;
    rt_elf_tls tls;
    uint64_t proc_param, proc_param_size;
    rt_elf_load *loads;
    uint64_t *load_offsets, *load_filesz;
    size_t load_count;
    reloc *relocs;
    size_t reloc_count;
    import *imports;
    size_t import_count, import_capacity;
    struct { unsigned id; int exported; char *name; } libraries[128];
    size_t library_count;
    struct { char *name; uint64_t value, size; int kind; } *exports;
    size_t export_count;
    uint64_t tls_module_id;
};

typedef struct {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
} phdr;

static void fail(char *error, size_t size, const char *fmt, ...)
{
    if (!error || !size) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(error, size, fmt, ap);
    va_end(ap);
}

static uint16_t u16(const unsigned char *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint32_t u32(const unsigned char *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t u64(const unsigned char *p) { uint64_t v; memcpy(&v, p, 8); return v; }

static int in_file(const rt_elf *e, uint64_t offset, uint64_t length)
{
    return offset <= e->file_size && length <= e->file_size - offset;
}

static long import_index(rt_elf *e, const char *name, int kind)
{
    for (size_t i = 0; i < e->import_count; i++)
        if (!strcmp(e->imports[i].name, name)) return (long)i;
    if (e->import_count == e->import_capacity) {
        size_t cap = e->import_capacity ? 2 * e->import_capacity : 256;
        import *grown = realloc(e->imports, cap * sizeof *grown);
        if (!grown) return -1;
        e->imports = grown;
        e->import_capacity = cap;
    }
    e->imports[e->import_count].name = strdup(name);
    e->imports[e->import_count].kind = kind;
    if (!e->imports[e->import_count].name) return -1;
    return (long)e->import_count++;
}

rt_elf *rt_elf_open(const char *path, char *error, size_t error_size)
{
    rt_elf *e = calloc(1, sizeof *e);
    FILE *f = fopen(path, "rb");
    if (!e || !f) {
        fail(error, error_size, "%s: cannot open", path);
        goto out;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    e->file = malloc(size > 0 ? (size_t)size : 1);
    if (size < 64 || !e->file || fread(e->file, 1, (size_t)size, f) != (size_t)size) {
        fail(error, error_size, "%s: cannot read", path);
        goto out;
    }
    fclose(f);
    f = NULL;
    e->file_size = (size_t)size;
    const unsigned char *d = e->file;
    if (memcmp(d, "\x7f" "ELF", 4) || d[4] != 2 || d[5] != 1) {
        fail(error, error_size, "%s: not a little-endian 64-bit ELF", path);
        goto out;
    }
    e->entry = u64(d + 0x18);
    const uint64_t phoff = u64(d + 0x20);
    const uint16_t phentsize = u16(d + 0x36), phnum = u16(d + 0x38);
    if (phentsize < 56 || !in_file(e, phoff, (uint64_t)phentsize * phnum)) {
        fail(error, error_size, "%s: bad program headers", path);
        goto out;
    }
    phdr dynamic = {0}, dynlib = {0};
    int have_dynamic = 0, have_dynlib = 0;
    e->loads = calloc(phnum, sizeof *e->loads);
    e->load_offsets = calloc(phnum, sizeof *e->load_offsets);
    e->load_filesz = calloc(phnum, sizeof *e->load_filesz);
    if (!e->loads || !e->load_offsets || !e->load_filesz) goto out;
    for (uint16_t i = 0; i < phnum; i++) {
        const unsigned char *p = d + phoff + (uint64_t)phentsize * i;
        phdr h = {u32(p), u32(p + 4), u64(p + 8), u64(p + 16), u64(p + 24), u64(p + 32), u64(p + 40), u64(p + 48)};
        if (h.type == PT_LOAD) {
            if (!in_file(e, h.offset, h.filesz) || h.filesz > h.memsz) {
                fail(error, error_size, "%s: segment %u outside the file", path, i);
                goto out;
            }
            e->loads[e->load_count] = (rt_elf_load){h.vaddr, h.memsz, h.flags};
            e->load_offsets[e->load_count] = h.offset;
            e->load_filesz[e->load_count] = h.filesz;
            e->load_count++;
            if (h.vaddr + h.memsz > e->image_size) e->image_size = h.vaddr + h.memsz;
        } else if (h.type == PT_SCE_PROCPARAM) {
            e->proc_param = h.vaddr;
            e->proc_param_size = h.memsz;
        } else if (h.type == PT_TLS) {
            e->tls = (rt_elf_tls){h.vaddr, h.filesz, h.memsz, h.align ? h.align : 1};
        } else if (h.type == PT_DYNAMIC) {
            dynamic = h;
            have_dynamic = 1;
        } else if (h.type == PT_SCE_DYNLIBDATA) {
            dynlib = h;
            have_dynlib = 1;
        }
    }
    if (!e->load_count || !have_dynamic || !have_dynlib || !in_file(e, dynamic.offset, dynamic.filesz) ||
        !in_file(e, dynlib.offset, dynlib.filesz)) {
        fail(error, error_size, "%s: no PT_LOAD, PT_DYNAMIC or PT_SCE_DYNLIBDATA", path);
        goto out;
    }

    uint64_t tag_value[8] = {0};
    int tag_seen[8] = {0};
    static const int64_t wanted[8] = {DT_SCE_STRTAB, DT_SCE_STRSZ, DT_SCE_SYMTAB, DT_SCE_SYMTABSZ,
                                      DT_SCE_RELA,   DT_SCE_RELASZ, DT_SCE_JMPREL, DT_SCE_PLTRELSZ};
    for (uint64_t i = 0; i + 16 <= dynamic.filesz; i += 16) {
        int64_t tag;
        memcpy(&tag, d + dynamic.offset + i, 8);
        if (tag == DT_NULL) break;
        for (int w = 0; w < 8; w++)
            if (tag == wanted[w] && !tag_seen[w]) {
                tag_value[w] = u64(d + dynamic.offset + i + 8);
                tag_seen[w] = 1;
            }
    }
    /* DT_SCE_IMPORT_LIB and DT_SCE_EXPORT_LIB: string offset in the low 32 bits, the library's id
       in the top 16 */
    for (uint64_t i = 0; i + 16 <= dynamic.filesz; i += 16) {
        int64_t tag;
        memcpy(&tag, d + dynamic.offset + i, 8);
        if (tag == DT_NULL) break;
        if ((tag != DT_SCE_IMPORT_LIB && tag != DT_SCE_EXPORT_LIB) ||
            e->library_count == sizeof e->libraries / sizeof e->libraries[0])
            continue;
        const uint64_t v = u64(d + dynamic.offset + i + 8);
        const uint64_t off = dynlib.offset + tag_value[0] + (uint32_t)v;
        if ((uint32_t)v >= tag_value[1] || !memchr(d + off, 0, tag_value[1] - (uint32_t)v)) continue;
        e->libraries[e->library_count].id = (unsigned)(v >> 48);
        e->libraries[e->library_count].exported = tag == DT_SCE_EXPORT_LIB;
        e->libraries[e->library_count].name = strdup((const char *)d + off);
        e->library_count++;
    }
    for (int w = 0; w < 8; w++)
        if (!tag_seen[w]) {
            fail(error, error_size, "%s: dynamic tag %#llx missing", path, (unsigned long long)wanted[w]);
            goto out;
        }
    const uint64_t base = dynlib.offset;
    const uint64_t strtab = base + tag_value[0], strsz = tag_value[1], symtab = base + tag_value[2], symsz = tag_value[3];
    if (!in_file(e, strtab, strsz) || !in_file(e, symtab, symsz) || !in_file(e, base + tag_value[4], tag_value[5]) ||
        !in_file(e, base + tag_value[6], tag_value[7])) {
        fail(error, error_size, "%s: dynamic tables outside the file", path);
        goto out;
    }
    const size_t symbols = symsz / 24;
    e->reloc_count = 0;
    e->relocs = malloc(((tag_value[5] + tag_value[7]) / 24 + 1) * sizeof *e->relocs);
    if (!e->relocs) goto out;
    const uint64_t tables[2][2] = {{base + tag_value[4], tag_value[5]}, {base + tag_value[6], tag_value[7]}};
    for (int t = 0; t < 2; t++)
        for (uint64_t i = 0; i + 24 <= tables[t][1]; i += 24) {
            const unsigned char *r = d + tables[t][0] + i;
            const uint64_t target = u64(r), info = u64(r + 8);
            int64_t addend;
            memcpy(&addend, r + 16, 8);
            const uint32_t type = (uint32_t)info, sym = (uint32_t)(info >> 32);
            reloc out = {target, KIND_LOCAL, 0, 0};
            if (type == R_X86_64_RELATIVE) {
                out.value = (uint64_t)addend;
            } else if (type == R_X86_64_DTPMOD64) {
                /* the TLS module id of a symbol of this module (dynamic TLS through __tls_get_addr):
                   none, a defined one, or a local one such as the unnamed section symbol. A named
                   global symbol defined elsewhere would be another module's, and is refused. */
                const unsigned char *ts = sym < symbols ? d + symtab + 24ull * sym : NULL;
                if (sym && (!ts || (!u16(ts + 6) && (ts[4] >> 4) != 0 && u32(ts) != 0))) {
                    fail(error, error_size, "%s: TLS module relocation at %#llx names another module's symbol", path,
                         (unsigned long long)target);
                    goto out;
                }
                out.kind = KIND_TLS_MODULE;
            } else if (type == R_X86_64_64 || type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT) {
                if (sym >= symbols) {
                    fail(error, error_size, "%s: relocation at %#llx names symbol %u of %zu", path,
                         (unsigned long long)target, sym, symbols);
                    goto out;
                }
                const unsigned char *s = d + symtab + 24ull * sym;
                const uint32_t name = u32(s);
                const uint8_t info8 = s[4];
                const uint16_t shndx = u16(s + 6);
                const uint64_t value = u64(s + 8);
                const int64_t extra = type == R_X86_64_64 ? addend : 0;
                if (shndx != 0) {
                    out.value = value + (uint64_t)extra;
                } else {
                    if (name >= strsz || !memchr(d + strtab + name, 0, strsz - name)) {
                        fail(error, error_size, "%s: bad symbol name for relocation at %#llx", path, (unsigned long long)target);
                        goto out;
                    }
                    const int kind = (info8 & 0xf) == STT_OBJECT ? RT_ELF_IMPORT_DATA : RT_ELF_IMPORT_FUNCTION;
                    const long index = import_index(e, (const char *)d + strtab + name, kind);
                    if (index < 0) goto out;
                    out.kind = KIND_IMPORT;
                    out.value = (uint64_t)index;
                    out.addend = kind == RT_ELF_IMPORT_DATA ? extra : 0;
                }
            } else {
                fail(error, error_size, "%s: relocation type %u at %#llx is not handled", path, type,
                     (unsigned long long)target);
                goto out;
            }
            if (out.target + 8 > e->image_size) {
                fail(error, error_size, "%s: relocation at %#llx outside the image", path, (unsigned long long)target);
                goto out;
            }
            e->relocs[e->reloc_count++] = out;
        }
    /* Exports: defined global or weak symbols named "NID#library#module" */
    e->exports = calloc(symbols ? symbols : 1, sizeof *e->exports);
    if (!e->exports) goto out;
    for (size_t i = 1; i < symbols; i++) {
        const unsigned char *s = d + symtab + 24ull * i;
        const uint32_t name = u32(s);
        const uint8_t bind = s[4] >> 4;
        if (!u16(s + 6) || (bind != 1 && bind != 2) || name >= strsz || !memchr(d + strtab + name, 0, strsz - name))
            continue;
        const char *n = (const char *)d + strtab + name;
        if (!strchr(n, '#')) continue;
        e->exports[e->export_count].name = strdup(n);
        if (!e->exports[e->export_count].name) goto out;
        e->exports[e->export_count].value = u64(s + 8);
        e->exports[e->export_count].size = u64(s + 16);
        e->exports[e->export_count].kind = (s[4] & 0xf) == STT_OBJECT ? RT_ELF_IMPORT_DATA : RT_ELF_IMPORT_FUNCTION;
        e->export_count++;
    }
    e->tls_module_id = 1;
    return e;
out:
    if (f) fclose(f);
    rt_elf_close(e);
    return NULL;
}

void rt_elf_close(rt_elf *e)
{
    if (!e) return;
    for (size_t i = 0; i < e->import_count; i++) free(e->imports[i].name);
    for (size_t i = 0; i < e->library_count; i++) free(e->libraries[i].name);
    for (size_t i = 0; i < e->export_count; i++) free(e->exports[i].name);
    free(e->exports);
    free(e->imports);
    free(e->relocs);
    free(e->loads);
    free(e->load_offsets);
    free(e->load_filesz);
    free(e->file);
    free(e);
}

uint64_t rt_elf_image_size(const rt_elf *e) { return e->image_size; }
uint64_t rt_elf_entry(const rt_elf *e) { return e->entry; }
const rt_elf_tls *rt_elf_tls_segment(const rt_elf *e) { return &e->tls; }
uint64_t rt_elf_proc_param(const rt_elf *e, uint64_t *size)
{
    if (size) *size = e->proc_param_size;
    return e->proc_param;
}
size_t rt_elf_load_count(const rt_elf *e) { return e->load_count; }
const rt_elf_load *rt_elf_loads(const rt_elf *e) { return e->loads; }
size_t rt_elf_relocation_count(const rt_elf *e) { return e->reloc_count; }
size_t rt_elf_import_count(const rt_elf *e) { return e->import_count; }
const char *rt_elf_import_name(const rt_elf *e, size_t i) { return i < e->import_count ? e->imports[i].name : NULL; }
int rt_elf_import_kind(const rt_elf *e, size_t i) { return i < e->import_count ? e->imports[i].kind : -1; }

/* "NID#L#M": L is the library id in the NID alphabet (A-Z a-z 0-9 + -), one or more digits;
   named by the import or export library table. */
static const char *library_of(const rt_elf *e, const char *name, int exported)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
    const char *p = strchr(name, '#');
    if (!p) return NULL;
    unsigned id = 0;
    for (p++; *p && *p != '#'; p++) {
        const char *at = strchr(alphabet, *p);
        if (!at) return NULL;
        id = id * 64 + (unsigned)(at - alphabet);
    }
    for (size_t l = 0; l < e->library_count; l++)
        if (e->libraries[l].id == id && e->libraries[l].exported == exported) return e->libraries[l].name;
    return NULL;
}

const char *rt_elf_import_library(const rt_elf *e, size_t i)
{
    return i < e->import_count ? library_of(e, e->imports[i].name, 0) : NULL;
}

size_t rt_elf_export_count(const rt_elf *e) { return e->export_count; }
const char *rt_elf_export_name(const rt_elf *e, size_t i) { return i < e->export_count ? e->exports[i].name : NULL; }
const char *rt_elf_export_library(const rt_elf *e, size_t i)
{
    return i < e->export_count ? library_of(e, e->exports[i].name, 1) : NULL;
}
uint64_t rt_elf_export_value(const rt_elf *e, size_t i) { return i < e->export_count ? e->exports[i].value : 0; }
uint64_t rt_elf_export_size(const rt_elf *e, size_t i) { return i < e->export_count ? e->exports[i].size : 0; }
int rt_elf_export_kind(const rt_elf *e, size_t i) { return i < e->export_count ? e->exports[i].kind : -1; }
void rt_elf_set_tls_module_id(rt_elf *e, uint64_t id) { e->tls_module_id = id; }

int rt_elf_map(const rt_elf *e, void *base, rt_elf_bind_fn bind, void *user, char *error, size_t error_size)
{
    unsigned char *image = base;
    memset(image, 0, e->image_size);
    for (size_t i = 0; i < e->load_count; i++)
        memcpy(image + e->loads[i].vaddr, e->file + e->load_offsets[i], e->load_filesz[i]);
    uint64_t *bound = calloc(e->import_count ? e->import_count : 1, sizeof *bound);
    if (!bound) {
        fail(error, error_size, "out of memory");
        return -1;
    }
    for (size_t i = 0; i < e->import_count; i++) {
        bound[i] = bind(user, i, e->imports[i].kind);
        if (!bound[i]) {
            fail(error, error_size, "import %s was not bound", e->imports[i].name);
            free(bound);
            return -1;
        }
    }
    for (size_t i = 0; i < e->reloc_count; i++) {
        const reloc *r = &e->relocs[i];
        const uint64_t v = r->kind == KIND_LOCAL         ? (uint64_t)(uintptr_t)image + r->value
                           : r->kind == KIND_TLS_MODULE ? e->tls_module_id
                                                        : bound[r->value] + (uint64_t)r->addend;
        memcpy(image + r->target, &v, 8);
    }
    free(bound);
    return 0;
}

size_t rt_elf_thread_pointer_to_gs(const rt_elf *e, void *base)
{
    static const unsigned char fs_load[9] = {0x64, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0};
    unsigned char *image = base;
    size_t changed = 0;
    for (size_t i = 0; i < e->load_count; i++) {
        if (!(e->loads[i].flags & 1)) continue;
        unsigned char *p = image + e->loads[i].vaddr, *end = p + e->load_filesz[i];
        while (p + sizeof fs_load <= end) {
            unsigned char *hit = memmem(p, (size_t)(end - p), fs_load, sizeof fs_load);
            if (!hit) break;
            hit[0] = 0x65;
            changed++;
            p = hit + sizeof fs_load;
        }
    }
    return changed;
}
