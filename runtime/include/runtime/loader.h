/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RUNTIME_LOADER_H
#define RUNTIME_LOADER_H

/* Loading a PS4 executable (ELF with SCE dynamic linking) into host memory: Paleblood's own
   loader, written from the published format. It reads the program headers and the string,
   symbol and relocation tables that PT_DYNAMIC's DT_SCE_* entries point into (inside
   PT_SCE_DYNLIBDATA), copies PT_LOAD and PT_SCE_RELRO allocated segments into memory the caller
   reserved, zero-fills the rest, and applies every relocation: addresses inside the executable become host addresses,
   and each imported function or object is bound to whatever address the caller's callback gives
   for it (an implementation, a stub, a data page).

   Used by the verification harness (tools/harness.py, through the shared library
   libpbloader.so) and, as the runtime grows, by the runtime itself. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rt_elf rt_elf;

/* Allocated image ranges, in program-header order: PT_LOAD and the separate SCE RELRO
   segment (0x61000010). Original flags are metadata; the loader never applies host protections.
   SCE RELRO overlap with another allocated range is unsupported and rejected at open. */
typedef struct {
    uint64_t vaddr;   /* offset from the image start (the executable is linked at 0) */
    uint64_t memsz;
    uint32_t flags;   /* PF_X 1, PF_W 2, PF_R 4 */
} rt_elf_load;

/* The thread-local storage template (PT_TLS): `filesz` bytes at `vaddr` in the image, zero-filled to
   `memsz`. All zero when the executable has none. */
typedef struct {
    uint64_t vaddr, filesz, memsz, align;
} rt_elf_tls;

enum { RT_ELF_IMPORT_FUNCTION = 0, RT_ELF_IMPORT_DATA = 1 };

/* Returns the host address an import is bound to: a function's entry, or an object's storage. */
typedef uint64_t (*rt_elf_bind_fn)(void *user, size_t import_index, int kind);

/* Reads and checks the executable. On failure returns NULL with a message in `error`. */
rt_elf *rt_elf_open(const char *path, char *error, size_t error_size);
void rt_elf_close(rt_elf *elf);

uint64_t rt_elf_image_size(const rt_elf *elf);   /* bytes from the image start to the end of the last segment */
uint64_t rt_elf_entry(const rt_elf *elf);
const rt_elf_tls *rt_elf_tls_segment(const rt_elf *elf);
/* The process parameter block (PT_SCE_PROCPARAM): its offset in the image, and its size in *size;
   0 and 0 when the executable has none. */
uint64_t rt_elf_proc_param(const rt_elf *elf, uint64_t *size);
size_t rt_elf_load_count(const rt_elf *elf);
const rt_elf_load *rt_elf_loads(const rt_elf *elf);
size_t rt_elf_relocation_count(const rt_elf *elf);

/* Imports, in the order their first relocation names them: "NID#library#module". */
size_t rt_elf_import_count(const rt_elf *elf);
const char *rt_elf_import_name(const rt_elf *elf, size_t index);
int rt_elf_import_kind(const rt_elf *elf, size_t index);
/* The library an import comes from ("libkernel", "libc", ...), or NULL if not named. */
const char *rt_elf_import_library(const rt_elf *elf, size_t index);

/* Exports (a module's defined symbols named "NID#library#module"): the name, the exporting
   library's name, the symbol's offset in the image, and RT_ELF_IMPORT_FUNCTION or _DATA. */
size_t rt_elf_export_count(const rt_elf *elf);
const char *rt_elf_export_name(const rt_elf *elf, size_t index);
const char *rt_elf_export_library(const rt_elf *elf, size_t index);
uint64_t rt_elf_export_value(const rt_elf *elf, size_t index);
uint64_t rt_elf_export_size(const rt_elf *elf, size_t index);   /* the symbol's size in bytes */
int rt_elf_export_kind(const rt_elf *elf, size_t index);

/* The module id written by R_X86_64_DTPMOD64 relocations (dynamic TLS); 1 unless set before
   rt_elf_map. */
void rt_elf_set_tls_module_id(rt_elf *elf, uint64_t id);

/* Copies all admitted image ranges into `base` (rt_elf_image_size bytes, writable,
   reserved by the caller),
   zero-fills, and applies the relocations, binding imports through `bind`. Segment flags do not
   change this writable-buffer contract or apply permissions. Returns 0, or -1 with
   a message in `error`. */
int rt_elf_map(const rt_elf *elf, void *base, rt_elf_bind_fn bind, void *user, char *error, size_t error_size);

/* Rewrite each exact 9-byte `mov rax, fs:[0]` pattern to the same-length
   `mov rax, gs:[offset]`, encoding offset as a little-endian 32-bit displacement.
   Only file-backed bytes of executable PT_LOAD segments are scanned. This is a byte-pattern
   scan, not instruction-boundary decoding: embedded matching bytes can also be rewritten.
   elf must be an open valid image; base must hold its complete, exclusively writable mapped
   image. changed must point to writable size_t storage disjoint from that image.
   Returns 0 and stores the match count (including zero), or -1 for a NULL argument or offset
   greater than INT32_MAX, leaving the image and any valid changed output untouched on rejection.
   No GS base, pthread key, executable-memory permissions or platform TLS state is changed. */
int rt_elf_thread_pointer_to_gs_offset(const rt_elf *elf, void *base, uint32_t offset, size_t *changed);

/* Zero-offset compatibility wrapper. On Linux the runtime points GS at the guest TCB;
   this retains the existing `mov rax, fs:[0]` -> `mov rax, gs:[0]` bytes and match count. */
size_t rt_elf_thread_pointer_to_gs(const rt_elf *elf, void *base);

#ifdef __cplusplus
}
#endif

#endif
