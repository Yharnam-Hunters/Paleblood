/* SPDX-License-Identifier: GPL-2.0-or-later */
/* pbboot: runs a PS4 executable's entry on Paleblood's runtime, as far as the runtime goes.

     pbboot EBOOT.elf [--module MODULE.elf]... [--stub-module ID:NAME]... [--app-param ID=VALUE]... [--system-param ID=VALUE]... [--app0 DIR] [--nids NID_DB.xml]
            [--status OUT.json] [--timeout SECONDS] [--trace]
     pbboot EBOOT.elf [--module MODULE.elf]... --imports [--nids NID_DB.xml]
            (list every import: importer, library, symbol, kind, provider, provider offset, size)

   The executable and any modules it ships with (--module, in start order) are mapped with our
   loader, each at its own base. An import is bound to a module's export with the same NID and
   library when one of the modules exports it, else to the runtime's implementation
   (runtime/syslib.h), else to a trampoline that stops the run and names it. Each module's entry
   (its constructors, then its start function) runs before the executable's entry.

   A module the title ships but that is deliberately not mapped is named with --stub-module, by its
   system module id and library name: when the guest asks the system module library to load it,
   the runtime reports it loaded without mapping it (runtime/syslib/kernel_sysmodule.c), and the
   run reports which of them were loaded.

   Every import call is counted, so the run also reports the boot milestones reached: the first
   call to each milestone's system functions (threads, files, input, audio, video out, GPU
   submission, the first flip; the runtime roadmap's order). Faults and timeouts are caught and
   reported with the faulting instruction's object and offset.

   Names: an import is "NID#library#module"; library names come from the object, symbol names
   from a public NID database (--nids, default $BB_NID_DB), else the NID is shown. */
#define _GNU_SOURCE
#include "runtime/loader.h"
#include "runtime/nid.h"
#include "runtime/process.h"
#include "runtime/syslib.h"

#include <asm/prctl.h>
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#define GUEST_BASE 0x800000000ull
#define MODULE_BASE 0x880000000ull
#define MODULE_STRIDE 0x4000000ull
#define MAX_OBJECTS 16
#define STACK_SIZE (16u << 20)
#define TRAMPOLINE_SIZE 48

static const struct {
    const char *name;
    const char *symbols[3];
} milestones[] = {
    {"entry", {NULL}},   /* reached when the executable makes its first import call */
    {"threads", {"scePthreadCreate"}},
    {"files", {"sceKernelOpen", "open"}},
    {"input", {"scePadOpen"}},
    {"audio", {"sceAudioOutOpen"}},
    {"video out", {"sceVideoOutOpen"}},
    {"gpu", {"sceGnmSubmitCommandBuffers"}},
    {"first flip", {"sceGnmSubmitAndFlipCommandBuffers", "sceVideoOutSubmitFlip"}},
};
#define MILESTONES (sizeof milestones / sizeof milestones[0])

enum provider { NONE, MODULE, RUNTIME, RUNTIME_UNRESOLVED };

typedef struct {
    const char *path, *label;
    rt_elf *elf;
    unsigned char *image, *tramp, *data;
    uint64_t size;
    size_t imports;
    void **bound;            /* per import: host address, or NULL */
    enum provider *provider;
    int *provider_object;    /* MODULE: which object */
    size_t *provider_export; /* MODULE: which export */
    unsigned char *called;   /* per import: called at least once */
} object;

static object objects[MAX_OBJECTS];
static size_t object_count;

enum outcome { RUNNING, UNIMPLEMENTED, FAULT, TIMEOUT, EXITED, RETURNED };
static const char *const outcome_names[] = {"running", "unimplemented", "fault", "timeout", "exited", "returned"};

static sigjmp_buf back_to_host;
static volatile enum outcome outcome = RUNNING;
/* The host thread that runs the boot. A stop on any other guest thread is recorded there and the
   main thread is told (SIGUSR1) to make the report; only the first stop counts. */
static pthread_t main_thread;
static atomic_int stopped = 0;
static volatile uint32_t stopped_at;            /* object << 20 | import index of the unimplemented call */
static volatile uint64_t stopped_from;          /* the guest's return address of that call */
static volatile uint64_t fault_rip, fault_address;
static volatile int fault_signal;
static volatile int exit_code;

/* Claims the run's one stop; false when another thread stopped first. */
static int claim_stop(void)
{
    int expected = 0;
    return atomic_compare_exchange_strong(&stopped, &expected, 1);
}

/* After a stop is recorded: unwind on the main thread; on another guest thread, wake the main one
   to make the report and wait for the process to end there. */
static _Noreturn void unwind(void)
{
    if (pthread_equal(pthread_self(), main_thread)) siglongjmp(back_to_host, 1);
    pthread_kill(main_thread, SIGUSR1);
    for (;;) pause();
}

/* Entered from an unimplemented import's trampoline (edi = object << 20 | import). Never returns. */
static __attribute__((noinline)) void unimplemented(uint32_t which)
{
    if (!claim_stop()) for (;;) pause();
    /* the trampoline jumped here, so the return address is the guest's call site */
    stopped_from = (uint64_t)(uintptr_t)__builtin_return_address(0);
    stopped_at = which;
    outcome = UNIMPLEMENTED;
    unwind();
}

static void guest_exit(int code)
{
    exit_code = code;
    outcome = EXITED;
    siglongjmp(back_to_host, 1);
}

static void on_signal(int sig, siginfo_t *info, void *context)
{
    const ucontext_t *uc = context;
    if (sig == SIGUSR1) siglongjmp(back_to_host, 1); /* a worker stopped; its outcome is recorded */
    if (!claim_stop()) for (;;) pause();
    if (sig == SIGALRM) {
        outcome = TIMEOUT;
    } else {
        outcome = FAULT;
        fault_signal = sig;
        fault_address = (uint64_t)(uintptr_t)info->si_addr;
    }
    fault_rip = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
    unwind();
}

/* Calls fn(a, b, c) on the guest stack: rt_boot_call(fn, a, stack_top, b, c). */
uint64_t rt_boot_call(void *fn, void *a, void *stack_top, void *b, void *c);
__asm__(".text\n"
        ".globl rt_boot_call\n"
        "rt_boot_call:\n"
        "  push %rbp\n  push %rbx\n  push %r12\n  push %r13\n  push %r14\n  push %r15\n"
        "  mov %rsp, %r12\n"
        "  mov %rdx, %rsp\n"
        "  mov %rdi, %rax\n"
        "  mov %rsi, %rdi\n"
        "  mov %rcx, %rsi\n"
        "  mov %r8, %rdx\n"
        "  call *%rax\n"
        "  mov %r12, %rsp\n"
        "  pop %r15\n  pop %r14\n  pop %r13\n  pop %r12\n  pop %rbx\n  pop %rbp\n"
        "  ret\n");

/* Runs fn until it returns, exits, faults, times out or reaches an unimplemented import. Kept
   apart from main so that no caller state is live across the jump back. */
static __attribute__((noinline)) void run_guest(void *fn, void *a, void *b, void *c, void *stack_top, int timeout)
{
    outcome = RUNNING;
    if (!sigsetjmp(back_to_host, 1)) {
        alarm((unsigned)timeout);
        rt_boot_call(fn, a, stack_top, b, c);
        outcome = RETURNED;
    }
    alarm(0);
}

/* Each import's call goes through a trampoline: mark it called, then jump to the provider, or
   into unimplemented() with the object and import. r11 is scratch in the System V convention and
   carries no argument; rax is left alone (variadic calls pass a count in al). */
static void write_trampoline(unsigned char *t, unsigned char *mark, uint32_t which, uint64_t target)
{
    unsigned char *p = t;
    const uint64_t m = (uint64_t)(uintptr_t)mark;
    *p++ = 0x49, *p++ = 0xbb, memcpy(p, &m, 8), p += 8;                /* movabs r11, mark */
    *p++ = 0x41, *p++ = 0xc6, *p++ = 0x03, *p++ = 0x01;               /* mov byte [r11], 1 */
    if (!target) {
        *p++ = 0xbf, memcpy(p, &which, 4), p += 4;                     /* mov edi, which */
        target = (uint64_t)(uintptr_t)unimplemented;
    }
    *p++ = 0x49, *p++ = 0xbb, memcpy(p, &target, 8), p += 8;          /* movabs r11, target */
    *p++ = 0x41, *p++ = 0xff, *p++ = 0xe3;                             /* jmp r11 */
}

typedef struct {
    char nid[RT_NID_SIZE];
    const char *symbol;
} nid_name;

static int by_nid(const void *a, const void *b) { return strcmp(((const nid_name *)a)->nid, ((const nid_name *)b)->nid); }

/* The NID database: <Entry obf="NID" sym="name"/> lines. Returns the count; *out sorted by NID. */
static size_t read_nid_db(const char *path, nid_name **out, char **text)
{
    *out = NULL;
    *text = NULL;
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *t = malloc((size_t)size + 1);
    if (!t || fread(t, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(t);
        return 0;
    }
    fclose(f);
    t[size] = 0;
    size_t n = 0, cap = 0;
    nid_name *names = NULL;
    for (char *p = strstr(t, "obf=\""); p; p = strstr(p, "obf=\"")) {
        char *nid = p + 5, *end = strchr(nid, '"');
        char *sym = end ? strstr(end, "sym=\"") : NULL;
        char *send = sym ? strchr(sym + 5, '"') : NULL;
        if (!send) break;
        p = send + 1;
        if (end - nid != RT_NID_SIZE - 1) continue;
        if (n == cap) {
            cap = cap ? 2 * cap : 4096;
            nid_name *grown = realloc(names, cap * sizeof *names);
            if (!grown) break;
            names = grown;
        }
        memcpy(names[n].nid, nid, RT_NID_SIZE - 1);
        names[n].nid[RT_NID_SIZE - 1] = 0;
        *send = 0;
        names[n].symbol = sym + 5;
        n++;
    }
    qsort(names, n, sizeof *names, by_nid);
    *out = names;
    *text = t;
    return n;
}

static nid_name *db;
static size_t db_count;

static const char *symbol_of(const char *import)
{
    nid_name key = {{0}, NULL};
    memcpy(key.nid, import, RT_NID_SIZE - 1);
    const nid_name *hit = db_count ? bsearch(&key, db, db_count, sizeof *db, by_nid) : NULL;
    return hit ? hit->symbol : NULL;
}

static int same_nid(const char *a, const char *b)
{
    return !strncmp(a, b, RT_NID_SIZE - 1) && a[RT_NID_SIZE - 1] == '#' && b[RT_NID_SIZE - 1] == '#';
}

static void json_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\') fputc('\\', f);
        if ((unsigned char)*s < 0x20) fprintf(f, "\\u%04x", *s);
        else fputc(*s, f);
    }
    fputc('"', f);
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Binding: modules' exports first, then the runtime's implementations. */
static void resolve(object *o)
{
    for (size_t i = 0; i < o->imports; i++) {
        const char *name = rt_elf_import_name(o->elf, i), *library = rt_elf_import_library(o->elf, i);
        for (size_t m = 1; m < object_count && !o->bound[i]; m++) {
            if (&objects[m] == o) continue;
            for (size_t e = 0; e < rt_elf_export_count(objects[m].elf); e++) {
                const char *lib = rt_elf_export_library(objects[m].elf, e);
                if (same_nid(name, rt_elf_export_name(objects[m].elf, e)) && library && lib && !strcmp(library, lib)) {
                    o->bound[i] = objects[m].image + rt_elf_export_value(objects[m].elf, e);
                    o->provider[i] = MODULE;
                    o->provider_object[i] = (int)m;
                    o->provider_export[i] = e;
                    break;
                }
            }
        }
        for (const rt_syslib_entry *s = rt_syslib_begin(); !o->bound[i] && s && s != rt_syslib_end(); s++) {
            char nid[RT_NID_SIZE + 1];
            rt_nid(s->symbol, nid);
            nid[RT_NID_SIZE - 1] = '#', nid[RT_NID_SIZE] = 0;
            if (!strncmp(name, nid, RT_NID_SIZE) && library && !strcmp(library, s->library)) {
                o->bound[i] = s->fn;
                o->provider[i] = s->capability == RT_SYSLIB_CAPABILITY_IMPLEMENTED
                                     ? RUNTIME
                                     : RUNTIME_UNRESOLVED;
            }
        }
    }
}

static uint64_t bind_import(void *user, size_t index, int kind)
{
    const object *o = user;
    if (kind == RT_ELF_IMPORT_DATA)
        return o->bound[index] ? (uint64_t)(uintptr_t)o->bound[index] : (uint64_t)(uintptr_t)(o->data + 4096 * index);
    return (uint64_t)(uintptr_t)(o->tramp + TRAMPOLINE_SIZE * index);
}

/* Where an address is: "label+0xoffset", or "host+0x..." */
static void where(uint64_t address, char *out, size_t size)
{
    for (size_t k = 0; k < object_count; k++) {
        const uint64_t base = (uint64_t)(uintptr_t)objects[k].image;
        if (address >= base && address < base + objects[k].size) {
            snprintf(out, size, "%s+0x%llx", objects[k].label, (unsigned long long)(address - base));
            return;
        }
    }
    snprintf(out, size, "host+0x%llx", (unsigned long long)address);
}

/* --app-param ID=VALUE, ID=unread or boot=unread (game/app_params.csv, through tools/boot.py).
   Returns 0 when it was added. */
static int add_app_param(const char *arg)
{
    const char *eq = strchr(arg, '=');
    if (!eq || !eq[1]) return -1;
    const int unread = !strcmp(eq + 1, "unread");
    if (!strncmp(arg, "boot=", 5)) {
        if (!unread || rt_process.app_boot_param_unread) return -1;
        rt_process.app_boot_param_unread = 1;
        return 0;
    }
    char *end;
    const long id = strtol(arg, &end, 0);
    if (end != eq || id < 0 || id > INT32_MAX || rt_process.app_param_count == RT_MAX_APP_PARAMS) return -1;
    long value = 0;
    if (!unread) {
        value = strtol(eq + 1, &end, 0);
        if (*end || value < INT32_MIN || value > INT32_MAX) return -1;
    }
    for (uint32_t i = 0; i < rt_process.app_param_count; i++)
        if (rt_process.app_params[i].id == id) return -1;
    rt_process.app_params[rt_process.app_param_count++] = (rt_app_param){(int32_t)id, unread, (int32_t)value};
    return 0;
}

/* --system-param ID=VALUE, a console system setting (tools/boot.py). Returns 0 when it was added. */
static int add_system_param(const char *arg)
{
    const char *eq = strchr(arg, '=');
    if (!eq || !eq[1] || rt_process.system_param_count == RT_MAX_SYSTEM_PARAMS) return -1;
    char *end;
    const long id = strtol(arg, &end, 0);
    if (end != eq || id < 0 || id > INT32_MAX) return -1;
    const long value = strtol(eq + 1, &end, 0);
    if (*end || value < INT32_MIN || value > INT32_MAX) return -1;
    for (uint32_t i = 0; i < rt_process.system_param_count; i++)
        if (rt_process.system_params[i].id == id) return -1;
    rt_process.system_params[rt_process.system_param_count++] = (rt_system_param){(int32_t)id, (int32_t)value};
    return 0;
}

/* --stub-module ID:NAME, the id in C notation (0x0038). Returns 0 when it was added. */
static int add_stub_module(const char *arg)
{
    char *end;
    const char *colon = strchr(arg, ':');
    if (!colon || !colon[1] || rt_process.stub_module_count == RT_MAX_STUB_MODULES) return -1;
    errno = 0;
    const unsigned long id = strtoul(arg, &end, 0);
    if (errno || end == arg || end != colon || id > UINT16_MAX) return -1;
    for (uint32_t i = 0; i < rt_process.stub_module_count; i++)
        if (rt_process.stub_modules[i].id == id) return -1;
    rt_process.stub_modules[rt_process.stub_module_count++] = (rt_stub_module){(uint16_t)id, colon + 1, 0};
    return 0;
}

static const char *usage =
    "usage: pbboot EBOOT.elf [--module MODULE.elf]... [--stub-module ID:NAME]... [--app-param ID=VALUE]... [--system-param ID=VALUE]... [--app0 DIR] [--nids NID_DB.xml]\n"
    "              [--status OUT.json] [--timeout S] [--trace]\n"
    "       pbboot EBOOT.elf [--module MODULE.elf]... --imports [--nids NID_DB.xml]\n";

int main(int argc, char **argv)
{
    const char *nid_path = getenv("BB_NID_DB"), *status_path = NULL;
    int timeout = 30, trace = 0, list = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--nids") && i + 1 < argc) nid_path = argv[++i];
        else if (!strcmp(argv[i], "--status") && i + 1 < argc) status_path = argv[++i];
        else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) timeout = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--module") && i + 1 < argc && object_count && object_count < MAX_OBJECTS)
            objects[object_count++].path = argv[++i];
        else if (!strcmp(argv[i], "--stub-module") && i + 1 < argc) {
            if (add_stub_module(argv[++i])) {
                fprintf(stderr, "pbboot: --stub-module %s: want a new ID:NAME, the id 16 bits\n", argv[i]);
                return 2;
            }
        }
        else if (!strcmp(argv[i], "--app0") && i + 1 < argc)
            rt_process.app0_dir = argv[++i];
        else if (!strcmp(argv[i], "--system-param") && i + 1 < argc) {
            if (add_system_param(argv[++i])) {
                fprintf(stderr, "pbboot: --system-param %s: want a new ID=VALUE\n", argv[i]);
                return 2;
            }
        }
        else if (!strcmp(argv[i], "--app-param") && i + 1 < argc) {
            if (add_app_param(argv[++i])) {
                fprintf(stderr, "pbboot: --app-param %s: want a new ID=VALUE, ID=unread or boot=unread\n", argv[i]);
                return 2;
            }
        }
        else if (!strcmp(argv[i], "--trace")) trace = 1;
        else if (!strcmp(argv[i], "--imports")) list = 1;
        else if (argv[i][0] != '-' && !object_count) objects[object_count++].path = argv[i];
        else {
            fputs(usage, stderr);
            return 2;
        }
    }
    if (!object_count) {
        fputs(usage, stderr);
        return 2;
    }
    char error[512];
    for (size_t k = 0; k < object_count; k++) {
        object *o = &objects[k];
        o->label = k ? base_name(o->path) : "eboot";
        o->elf = rt_elf_open(o->path, error, sizeof error);
        if (!o->elf) {
            fprintf(stderr, "pbboot: %s\n", error);
            return 3;
        }
        o->size = rt_elf_image_size(o->elf);
        if (k && o->size > MODULE_STRIDE) {
            fprintf(stderr, "pbboot: %s: image larger than a module slot\n", o->path);
            return 3;
        }
        /* the address the object will have, known before mapping so exports can be bound */
        o->image = (unsigned char *)(uintptr_t)(k ? MODULE_BASE + (k - 1) * MODULE_STRIDE : GUEST_BASE);
        o->imports = rt_elf_import_count(o->elf);
        const size_t n = o->imports ? o->imports : 1;
        o->bound = calloc(n, sizeof *o->bound);
        o->provider = calloc(n, sizeof *o->provider);
        o->provider_object = calloc(n, sizeof *o->provider_object);
        o->provider_export = calloc(n, sizeof *o->provider_export);
        o->called = calloc(n, 1);
        rt_elf_set_tls_module_id(o->elf, k + 1);
    }
    char *db_text;
    db_count = read_nid_db(nid_path, &db, &db_text);
    for (size_t k = 0; k < object_count; k++) resolve(&objects[k]);

    if (list) {
        for (size_t k = 0; k < object_count; k++) {
            const object *o = &objects[k];
            for (size_t i = 0; i < o->imports; i++) {
                const char *name = rt_elf_import_name(o->elf, i), *sym = symbol_of(name);
                const char *lib = rt_elf_import_library(o->elf, i);
                const char *provider = o->provider[i] == MODULE    ? objects[o->provider_object[i]].label
                                       : o->provider[i] == RUNTIME ? "runtime"
                                       : o->provider[i] == RUNTIME_UNRESOLVED ? "runtime-unresolved"
                                                                             : "";
                unsigned long long offset = 0, size = 0;
                if (o->provider[i] == MODULE) {
                    const rt_elf *pe = objects[o->provider_object[i]].elf;
                    offset = rt_elf_export_value(pe, o->provider_export[i]);
                    size = rt_elf_export_size(pe, o->provider_export[i]);
                }
                printf("%s\t%s\t%s\t%s\t%s\t0x%08llx\t%llu\n", o->label, lib ? lib : "?", sym ? sym : name,
                       rt_elf_import_kind(o->elf, i) == RT_ELF_IMPORT_DATA ? "data" : "function", provider, offset, size);
            }
        }
        return 0;
    }

    /* Memory: each object's image, its trampolines and data imports; the guest stack and TLS. */
    for (size_t k = 0; k < object_count; k++) {
        object *o = &objects[k];
        void *want = o->image;
        o->image = mmap(want, o->size, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        o->tramp = mmap(NULL, (o->imports + 1) * TRAMPOLINE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        o->data = mmap(NULL, (o->imports + 1) * 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (o->image != want || o->tramp == MAP_FAILED || o->data == MAP_FAILED) {
            fprintf(stderr, "pbboot: cannot map memory for %s: %s\n", o->path, strerror(errno));
            return 3;
        }
        for (size_t i = 0; i < o->imports; i++)
            if (rt_elf_import_kind(o->elf, i) == RT_ELF_IMPORT_FUNCTION)
                write_trampoline(o->tramp + i * TRAMPOLINE_SIZE, &o->called[i], (uint32_t)(k << 20 | i),
                                 (uint64_t)(uintptr_t)o->bound[i]);
        if (rt_elf_map(o->elf, o->image, bind_import, o, error, sizeof error)) {
            fprintf(stderr, "pbboot: %s\n", error);
            return 3;
        }
        rt_elf_thread_pointer_to_gs(o->elf, o->image);
    }
    uint64_t proc_param_size;
    const uint64_t proc_param = rt_elf_proc_param(objects[0].elf, &proc_param_size);
    rt_process.image = objects[0].image;
    rt_process.image_size = objects[0].size;
    rt_process.proc_param = proc_param_size ? objects[0].image + proc_param : NULL;

    unsigned char *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        fprintf(stderr, "pbboot: cannot map the stack: %s\n", strerror(errno));
        return 3;
    }

    /* Thread pointer (x86-64 variant II): the executable's TLS block just below the thread control
       block, whose first word points to itself. Modules reach their TLS through __tls_get_addr. */
    const rt_elf_tls *tls = rt_elf_tls_segment(objects[0].elf);
    rt_process.tls_image = objects[0].image + tls->vaddr;
    rt_process.tls_filesz = tls->filesz;
    rt_process.tls_memsz = tls->memsz;
    rt_process.tls_align = tls->align;
    unsigned char *tp = rt_tls_block_create();
    if (!tp) {
        fprintf(stderr, "pbboot: no memory for the thread-local storage block\n");
        return 3;
    }
    if (syscall(SYS_arch_prctl, ARCH_SET_GS, (unsigned long)(uintptr_t)tp)) {
        fprintf(stderr, "pbboot: arch_prctl: %s\n", strerror(errno));
        return 3;
    }

    static unsigned char alt[1 << 16];
    stack_t ss = {.ss_sp = alt, .ss_size = sizeof alt};
    sigaltstack(&ss, NULL);
    struct sigaction sa = {0};
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGALRM, SIGUSR1};
    main_thread = pthread_self();
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; i++) sigaction(signals[i], &sa, NULL);

    unsigned char *stack_top = stack + STACK_SIZE - 64;
    rt_thread_register_main(stack, STACK_SIZE);
    /* Modules first, in the order given: entry(argument size 0, no arguments, no start override)
       runs the module's constructors and then its start function. */
    const char *phase = "eboot";
    for (size_t k = 1; k < object_count; k++) {
        phase = objects[k].label;
        run_guest(objects[k].image + rt_elf_entry(objects[k].elf), NULL, NULL, NULL, stack_top, timeout);
        if (outcome != RETURNED) break;
    }
    if (object_count == 1 || outcome == RETURNED) {
        /* The process entry receives a parameter block (argc, then argv) and an exit function. */
        static struct {
            int32_t argc;
            uint32_t pad;
            const char *argv[3];
        } params = {1, 0, {"eboot.bin", NULL, NULL}};
        phase = "eboot";
        run_guest(objects[0].image + rt_elf_entry(objects[0].elf), &params, (void *)guest_exit, NULL, stack_top, timeout);
    }

    /* Results */
    size_t called_total = 0;
    for (size_t k = 0; k < object_count; k++)
        for (size_t i = 0; i < objects[k].imports; i++) called_total += objects[k].called[i];
    int reached = -1;
    for (size_t m = 0; m < MILESTONES; m++) {
        int hit = 0;
        if (m == 0) {
            for (size_t i = 0; i < objects[0].imports && !hit; i++) hit = objects[0].called[i];
        }
        for (int s = 0; !hit && s < 3 && milestones[m].symbols[s]; s++) {
            char nid[RT_NID_SIZE];
            rt_nid(milestones[m].symbols[s], nid);
            for (size_t k = 0; k < object_count && !hit; k++)
                for (size_t i = 0; i < objects[k].imports && !hit; i++)
                    hit = objects[k].called[i] && !strncmp(rt_elf_import_name(objects[k].elf, i), nid, RT_NID_SIZE - 1);
        }
        if (hit) reached = (int)m;
    }
    size_t from_modules = 0, from_runtime = 0, from_unresolved = 0;
    for (size_t i = 0; i < objects[0].imports; i++) {
        from_modules += objects[0].provider[i] == MODULE;
        from_runtime += objects[0].provider[i] == RUNTIME;
        from_unresolved += objects[0].provider[i] == RUNTIME_UNRESOLVED;
    }

    const char *stop_name = NULL, *stop_library = NULL, *stop_caller = NULL;
    char stop_nid[RT_NID_SIZE] = "";
    if (outcome == UNIMPLEMENTED) {
        const object *o = &objects[stopped_at >> 20];
        const size_t i = stopped_at & 0xfffff;
        const char *name = rt_elf_import_name(o->elf, i);
        memcpy(stop_nid, name, RT_NID_SIZE - 1);
        stop_name = symbol_of(name);
        stop_library = rt_elf_import_library(o->elf, i);
        stop_caller = o->label;
    }
    char at[128] = "", from[128] = "";
    if (outcome == FAULT || outcome == TIMEOUT) where(fault_rip, at, sizeof at);
    if (outcome == UNIMPLEMENTED) where(stopped_from, from, sizeof from);

    printf("pbboot: %s: %s", phase, outcome_names[outcome]);
    if (outcome == UNIMPLEMENTED)
        printf(": %s calls %s %s (NID %s), returning to %s", stop_caller, stop_library ? stop_library : "?",
               stop_name ? stop_name : stop_nid, stop_nid, from);
    if (outcome == FAULT) printf(": signal %d at %s, address 0x%llx", fault_signal, at, (unsigned long long)fault_address);
    if (outcome == TIMEOUT) printf(" after %d s, at %s", timeout, at);
    if (outcome == EXITED) printf(" with code %d", exit_code);
    printf("\npbboot: milestone %s; executable imports %zu: %zu from bundled modules, %zu implemented by the runtime, "
           "%zu unresolved diagnostics, %zu unbound; imports called %zu\n",
           reached >= 0 ? milestones[reached].name : "loaded", objects[0].imports, from_modules, from_runtime,
           from_unresolved, objects[0].imports - from_modules - from_runtime - from_unresolved, called_total);
    if (rt_process.stub_module_count) {
        printf("pbboot: stub modules loaded:");
        size_t loaded = 0;
        for (uint32_t i = 0; i < rt_process.stub_module_count; i++)
            if (rt_process.stub_modules[i].loaded) printf(" %s", rt_process.stub_modules[i].name), loaded++;
        printf("%s (%zu of %u)\n", loaded ? "" : " none", loaded, rt_process.stub_module_count);
    }
    if (trace)
        for (size_t k = 0; k < object_count; k++)
            for (size_t i = 0; i < objects[k].imports; i++)
                if (objects[k].called[i]) {
                    const char *name = rt_elf_import_name(objects[k].elf, i), *sym = symbol_of(name);
                    const char *lib = rt_elf_import_library(objects[k].elf, i);
                    printf("  %s called %s %s\n", objects[k].label, lib ? lib : "?", sym ? sym : name);
                }

    if (status_path) {
        FILE *f = fopen(status_path, "w");
        if (!f) {
            fprintf(stderr, "pbboot: cannot write %s\n", status_path);
            return 3;
        }
        fprintf(f, "{\n  \"schema\": 2,\n  \"phase\": ");
        json_string(f, phase);
        fprintf(f, ",\n  \"outcome\": \"%s\",\n  \"milestone\": ", outcome_names[outcome]);
        json_string(f, reached >= 0 ? milestones[reached].name : "loaded");
        fprintf(f, ",\n  \"milestone_index\": %d,\n  \"milestones\": [\"loaded\"", reached + 1);
        for (size_t m = 0; m < MILESTONES; m++) fprintf(f, ", \"%s\"", milestones[m].name);
        fprintf(f,
                "],\n  \"imports_total\": %zu,\n  \"imports_implemented\": %zu,\n  \"imports_bundled\": %zu,\n"
                "  \"imports_unresolved\": %zu,\n  \"imports_remaining\": %zu,\n  \"imports_called\": %zu,\n",
                objects[0].imports, from_runtime, from_modules, from_unresolved,
                objects[0].imports - from_modules - from_runtime, called_total);
        fprintf(f, "  \"modules\": [");
        for (size_t k = 1; k < object_count; k++) {
            size_t remaining = 0;
            for (size_t i = 0; i < objects[k].imports; i++)
                remaining += objects[k].provider[i] == NONE || objects[k].provider[i] == RUNTIME_UNRESOLVED;
            fprintf(f, "%s{\"name\": ", k > 1 ? ", " : "");
            json_string(f, objects[k].label);
            fprintf(f, ", \"imports\": %zu, \"imports_remaining\": %zu, \"exports\": %zu}", objects[k].imports, remaining,
                    rt_elf_export_count(objects[k].elf));
        }
        fprintf(f, "],\n  \"stub_modules\": [");
        for (uint32_t i = 0; i < rt_process.stub_module_count; i++) {
            const rt_stub_module *m = &rt_process.stub_modules[i];
            fprintf(f, "%s{\"name\": ", i ? ", " : "");
            json_string(f, m->name);
            fprintf(f, ", \"id\": \"0x%04x\", \"loaded\": %s}", (unsigned)m->id, m->loaded ? "true" : "false");
        }
        fprintf(f, "],\n  \"first_unimplemented\": ");
        if (outcome == UNIMPLEMENTED) {
            fprintf(f, "{\"caller\": ");
            json_string(f, stop_caller);
            fprintf(f, ", \"library\": ");
            json_string(f, stop_library ? stop_library : "");
            fprintf(f, ", \"symbol\": ");
            json_string(f, stop_name ? stop_name : "");
            fprintf(f, ", \"nid\": ");
            json_string(f, stop_nid);
            fprintf(f, ", \"returns_to\": ");
            json_string(f, from);
            fprintf(f, "}");
        } else {
            fprintf(f, "null");
        }
        fprintf(f, ",\n  \"fault\": ");
        if (outcome == FAULT || outcome == TIMEOUT) {
            fprintf(f, "{\"signal\": %d, \"at\": ", fault_signal);
            json_string(f, at);
            fprintf(f, ", \"address\": \"0x%llx\"}", (unsigned long long)fault_address);
        } else {
            fprintf(f, "null");
        }
        fprintf(f, "\n}\n");
        fclose(f);
    }
    free(db);
    free(db_text);
    return 0;
}
