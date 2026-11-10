/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: save data (libSceSaveData). The title's saves, kept in the host directory the
   person running the runtime chose (rt_process.savedata_dir, --savedata; tools/boot.py gives each
   run a new empty one unless told to keep saves).

   Facts, from shadPS4's libSceSaveData (savedata.cpp, savedata.h, savedata_error.h,
   save_instance.cpp, save_memory.cpp), checked against bbport's and bbhost's and the OpenOrbis
   headers; implemented here independently. Where they disagree, shadPS4's answer is taken (it is
   the most complete) and the disagreement is noted.
   - Structures (the original, not the "2", entry points): mount request 80 bytes { user id,
     title id *, directory name *, fingerprint *, blocks, mode, reserved }; mount result 64 bytes,
     its mount point string first; directory search condition 64 bytes and result 56 bytes; the
     param 1328 bytes { title[128], subtitle[128], detail[1024], user param, mtime, reserved };
     the icon 56 bytes { buffer, buffer size, data size, reserved }.
   - Mount modes: 1 read-only, 2 read-write, 4 create (EXISTS if it is there), 8 destruct-off,
     0x10 copy the package's icon, 0x20 create or open (only for titles built against SDK 4.50 or
     later). A mount is served as /savedataN, N the lowest free of 16.
   - Errors: 0x809F0000 parameter, 0x809F0003 busy (the directory is already mounted), 0x809F0004
     not mounted, 0x809F0007 exists, 0x809F0008 not found, 0x809F000C mount full, 0x809F000D bad
     mounted (an icon to a read-only mount), 0x809F0011 invalid login user, 0x809F0012 memory not
     set up. "Not initialized" depends on the title's SDK: 0x809F000B before 2.00, 0x80960002
     before 2.50, 0x809F0001 from 2.50 (rt_compiled_sdk_version).
   - For a title built before SDK 4.50 a mount writes only the mount point back; before 4.00,
     terminating with saves mounted unmounts them; from 1.70 a directory search reports the total
     hit count, the number set and the params, and from 2.50 the block counts too.

   Layout on the host: <savedata dir>/<user id>/<directory name>/ holds the title's files, with
   sce_sys/param.bin (the 1328-byte param) and sce_sys/icon0.png; a save exists when its sce_sys
   does. The memory-backed save is <savedata dir>/<user id>/sce_sdmemory/memory.dat with its own
   sce_sys/param.bin. These are the runtime's own files (not a console's param.sfo).

   Not modelled, so stopping by name: another title's saves (a title id), the free-space check and
   its required-blocks answer, the backup copy and its "corrupted" marker, sort key 4, and a
   memory read or write past the size set up (shadPS4 clamps or grows, bbport answers parameter). */
#include "runtime/mounts.h"
#include "runtime/process.h"
#include "runtime/sdk.h"
#include "runtime/syslib.h"
#include "runtime/user.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SAVE_ERROR_PARAMETER ((int32_t)0x809F0000u)
#define SAVE_ERROR_BUSY ((int32_t)0x809F0003u)
#define SAVE_ERROR_NOT_MOUNTED ((int32_t)0x809F0004u)
#define SAVE_ERROR_EXISTS ((int32_t)0x809F0007u)
#define SAVE_ERROR_NOT_FOUND ((int32_t)0x809F0008u)
#define SAVE_ERROR_MOUNT_FULL ((int32_t)0x809F000Cu)
#define SAVE_ERROR_BAD_MOUNTED ((int32_t)0x809F000Du)
#define SAVE_ERROR_INVALID_LOGIN_USER ((int32_t)0x809F0011u)
#define SAVE_ERROR_MEMORY_NOT_READY ((int32_t)0x809F0012u)

enum {
    MODE_READ_ONLY = 0x1, MODE_READ_WRITE = 0x2, MODE_CREATE = 0x4, MODE_DESTRUCT_OFF = 0x8,
    MODE_COPY_ICON = 0x10, MODE_CREATE_OR_OPEN = 0x20,
    MAX_MOUNTS = 16, DIR_NAME_BYTES = 32, MOUNT_POINT_BYTES = 16,
    PARAM_TITLE = 1, PARAM_SUBTITLE = 2, PARAM_DETAIL = 3, PARAM_USER = 4, PARAM_ALL = 0,
    SORT_DIR_NAME = 0, SORT_USER_PARAM = 1, SORT_BLOCKS = 2, SORT_MTIME = 3, SORT_FREE_BLOCKS = 5,
    ORDER_ASCENT = 0, ORDER_DESCENT = 1,
};
enum { SDK_1_70 = 0x01700000, SDK_2_00 = 0x02000000, SDK_2_50 = 0x02500000, SDK_4_00 = 0x04000000,
       SDK_4_50 = 0x04500000 };

typedef struct {
    char title[128], subtitle[128], detail[1024];
    uint32_t user_param;
    uint32_t padding;
    int64_t mtime;
    uint8_t reserved[32];
} save_param;
_Static_assert(sizeof(save_param) == 1328, "the save data param is 1328 bytes");

typedef struct {
    int32_t user;
    uint32_t padding;
    const char *title_id, *dir_name;
    const void *fingerprint;
    uint64_t blocks;
    uint32_t mode;
    uint8_t reserved[36];
} mount_request;
_Static_assert(sizeof(mount_request) == 80 && offsetof(mount_request, mode) == 40, "the original mount request");

typedef struct {
    char mount_point[MOUNT_POINT_BYTES];
    uint64_t required_blocks;
    uint32_t unused, mount_status;
    uint8_t reserved[32];
} mount_result;
_Static_assert(sizeof(mount_result) == 64, "the mount result is 64 bytes");

typedef struct {
    int32_t user;
    uint32_t padding;
    const char *title_id, *dir_name;
    uint32_t key, order;
    uint8_t reserved[32];
} search_condition;
_Static_assert(sizeof(search_condition) == 64, "the search condition is 64 bytes");

typedef struct {
    uint32_t hit_count;
    uint32_t padding;
    char (*dir_names)[DIR_NAME_BYTES];
    uint32_t dir_names_capacity, set_count;
    save_param *params;
    void *infos;
    uint8_t reserved[16];
} search_result;
_Static_assert(sizeof(search_result) == 56 && offsetof(search_result, infos) == 32, "the search result is 56 bytes");

typedef struct {
    const void *buffer;
    uint64_t buffer_size, data_size;
    uint8_t reserved[32];
} save_icon;
_Static_assert(sizeof(save_icon) == 56, "the icon is 56 bytes");

typedef struct {
    int used, read_only;
    char dir_name[DIR_NAME_BYTES];
    char host_dir[4096];
} save_mount;

/* The title's threads call these on their own stacks, which can be small: path and copy buffers
   are static, used only while save_lock is held, never on the stack. */
static pthread_mutex_t save_lock = PTHREAD_MUTEX_INITIALIZER;
static int initialized;
static save_mount mounts[MAX_MOUNTS];
static uint64_t memory_size; /* the memory-backed save's size, 0 until it is set up */

static int32_t not_initialized(void)
{
    const uint32_t sdk = rt_compiled_sdk_version();
    return sdk < SDK_2_00 ? (int32_t)0x809F000Bu : sdk < SDK_2_50 ? (int32_t)0x80960002u : (int32_t)0x809F0001u;
}

/* Builds a host path; one that does not fit the buffer stops by name instead of being cut short. */
static void path_printf(const char *caller, char *out, size_t size, const char *format, ...)
    __attribute__((format(printf, 4, 5)));
static void path_printf(const char *caller, char *out, size_t size, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    const int n = vsnprintf(out, size, format, arguments);
    va_end(arguments);
    if (n < 0 || (size_t)n >= size) rt_fatal(caller, "a path longer than the runtime's buffer");
}

/* <savedata dir>/<user>, with an optional leaf; stops when the runtime was given no directory. */
static void user_path(const char *caller, const char *leaf, char *out, size_t size)
{
    if (!rt_process.savedata_dir) rt_fatal(caller, "no save data directory given (tools/boot.py --savedata)");
    path_printf(caller, out, size, "%s/%d%s%s", rt_process.savedata_dir, (int)RT_USER_ID, leaf ? "/" : "", leaf ? leaf : "");
}

static int make_directories(const char *path) /* mkdir -p */
{
    static char partial[4096];
    path_printf("save data", partial, sizeof partial, "%s", path);
    for (char *p = partial + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(partial, 0755) && errno != EEXIST) return -1;
        *p = '/';
    }
    return mkdir(partial, 0755) && errno != EEXIST ? -1 : 0;
}

static int write_file(const char *path, const void *data, size_t size)
{
    static char temporary[4200];
    path_printf("save data", temporary, sizeof temporary, "%s.new", path);
    FILE *f = fopen(temporary, "wb");
    if (!f) return -1;
    const int ok = fwrite(data, 1, size, f) == size;
    if (fclose(f) || !ok) return -1;
    return rename(temporary, path); /* whole or not at all */
}

/* A directory name is 1 to 31 characters, terminated within its 32 bytes, and usable as one host
   path component: no '/' or '\', not starting with '.' (bbport's check). */
static int valid_dir_name(const char *name)
{
    if (!name || memchr(name, 0, DIR_NAME_BYTES) == NULL || !name[0] || name[0] == '.') return 0;
    return !strpbrk(name, "/\\");
}

static int path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int read_param(const char *save_dir, save_param *out)
{
    static char path[4300];
    path_printf("save data", path, sizeof path, "%s/sce_sys/param.bin", save_dir);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    const int ok = fread(out, 1, sizeof *out, f) == sizeof *out;
    fclose(f);
    return ok ? 0 : -1;
}

static int write_param(const char *save_dir, const save_param *param)
{
    static char path[4300];
    path_printf("save data", path, sizeof path, "%s/sce_sys", save_dir);
    if (make_directories(path)) return -1;
    path_printf("save data", path, sizeof path, "%s/sce_sys/param.bin", save_dir);
    return write_file(path, param, sizeof *param);
}

static int32_t check_user(const char *caller, int32_t user)
{
    if (user < 0) return SAVE_ERROR_INVALID_LOGIN_USER;
    if (user != RT_USER_ID) rt_fatal(caller, "a user the runtime did not hand out: the answer is not established");
    return 0;
}

static int32_t initialize(const void *params)
{
    (void)params; /* a priority block; the library ignores it (shadPS4, bbport) */
    pthread_mutex_lock(&save_lock);
    initialized = 1;
    pthread_mutex_unlock(&save_lock);
    return 0;
}

static void unmount_slot(int slot) /* with save_lock held */
{
    char point[RT_MOUNT_POINT_BYTES];
    snprintf(point, sizeof point, "/savedata%d", slot);
    rt_mount_remove(point);
    mounts[slot].used = 0;
}

static int32_t terminate(void)
{
    static const char fn[] = "sceSaveDataTerminate";
    pthread_mutex_lock(&save_lock);
    int32_t result = initialized ? 0 : not_initialized();
    for (int slot = 0; !result && slot < MAX_MOUNTS; slot++) {
        if (!mounts[slot].used) continue;
        if (rt_compiled_sdk_version() >= SDK_4_00) rt_fatal(fn, "terminating with saves mounted: busy, from SDK 4.00, is not modelled");
        unmount_slot(slot); /* before SDK 4.00 the library unmounts them */
    }
    if (!result) initialized = 0;
    pthread_mutex_unlock(&save_lock);
    return result;
}

static int32_t mount_save(const mount_request *request, mount_result *result)
{
    static const char fn[] = "sceSaveDataMount";
    pthread_mutex_lock(&save_lock);
    int32_t error = !initialized ? not_initialized() : (!request || !request->dir_name || !result) ? SAVE_ERROR_PARAMETER : 0;
    if (!error) error = check_user(fn, request->user);
    if (!error && !valid_dir_name(request->dir_name)) error = SAVE_ERROR_PARAMETER;
    if (error) goto done;
    if (request->title_id) rt_fatal(fn, "another title's saves (a title id) are not modelled");
    const uint32_t known = MODE_READ_ONLY | MODE_READ_WRITE | MODE_CREATE | MODE_DESTRUCT_OFF | MODE_COPY_ICON | MODE_CREATE_OR_OPEN;
    if (request->mode & ~known) rt_fatal(fn, "a mount mode bit that is not established");
    const int create = (request->mode & MODE_CREATE) != 0;
    const int create_or_open = (request->mode & MODE_CREATE_OR_OPEN) && rt_compiled_sdk_version() >= SDK_4_50;
    int free_slot = -1;
    for (int slot = 0; slot < MAX_MOUNTS; slot++) {
        if (mounts[slot].used && !strcmp(mounts[slot].dir_name, request->dir_name)) error = SAVE_ERROR_BUSY;
        if (!mounts[slot].used && free_slot < 0) free_slot = slot;
    }
    if (!error && free_slot < 0) error = SAVE_ERROR_MOUNT_FULL;
    if (error) goto done;
    static char save_dir[4096], sce_sys[4200];
    user_path(fn, request->dir_name, save_dir, sizeof save_dir);
    path_printf(fn, sce_sys, sizeof sce_sys, "%s/sce_sys", save_dir);
    const int exists = path_exists(sce_sys);
    if (!exists && !create && !create_or_open) error = SAVE_ERROR_NOT_FOUND;
    else if (exists && create) error = SAVE_ERROR_EXISTS;
    if (error) goto done;
    if (!exists) {
        save_param fresh = {.mtime = (int64_t)time(NULL)};
        if (make_directories(save_dir) || write_param(save_dir, &fresh)) rt_fatal(fn, "the host could not create the save directory");
        static char from[4200], to[4300];
        if ((request->mode & MODE_COPY_ICON) && rt_process.app0_dir) {
            path_printf(fn, from, sizeof from, "%s/sce_sys/save_data.png", rt_process.app0_dir);
            path_printf(fn, to, sizeof to, "%s/icon0.png", sce_sys);
            FILE *in = fopen(from, "rb");
            if (in) {
                static char buffer[65536];
                FILE *out = fopen(to, "wb");
                for (size_t n; out && (n = fread(buffer, 1, sizeof buffer, in)) > 0;) fwrite(buffer, 1, n, out);
                if (out) fclose(out);
                fclose(in);
            }
        }
    }
    const int read_only = (request->mode & MODE_READ_ONLY) && !(request->mode & MODE_READ_WRITE);
    char point[RT_MOUNT_POINT_BYTES];
    snprintf(point, sizeof point, "/savedata%d", free_slot);
    if (rt_mount_add(point, save_dir, read_only)) rt_fatal(fn, "the runtime's mount table refused the save");
    mounts[free_slot] = (save_mount){.used = 1, .read_only = read_only};
    path_printf(fn, mounts[free_slot].dir_name, sizeof mounts[free_slot].dir_name, "%s", request->dir_name);
    path_printf(fn, mounts[free_slot].host_dir, sizeof mounts[free_slot].host_dir, "%s", save_dir);
    memcpy(result->mount_point, point, MOUNT_POINT_BYTES); /* before SDK 4.50 nothing else is written */
done:
    pthread_mutex_unlock(&save_lock);
    return error;
}

/* sceSaveDataDelete(const delete request *): the 64-byte request { user id, padding, title id *,
   directory name *, unused, reserved } (shadPS4's OrbisSaveDataDelete). The executable's one caller
   (0x02c3e765) clears it, copies the directory name into a 32-byte buffer it points at, passes no
   title id, and asks again while the answer is 0x809F0014. Answers, as for a mount: not
   initialized, no request or no directory name and a bad name 0x809F0000, a user below 0
   0x809F0011, a save that is mounted 0x809F0003 (busy). Otherwise the save's folder and everything
   in it are removed and the answer is 0. Stops by name: another title's saves, a save that is not
   there (shadPS4 answers 0, which is not established for the console), and a host failure. */
typedef struct {
    int32_t user;
    uint32_t padding;
    const char *title_id, *dir_name;
    uint32_t unused;
    uint8_t reserved[32];
    uint32_t tail;
} delete_request;
_Static_assert(sizeof(delete_request) == 64 && offsetof(delete_request, dir_name) == 16, "the delete request");

/* Removes the folder at path (a buffer of size bytes, the path's own text) and everything in it,
   depth first, extending and restoring the one buffer as it goes, so the stack holds only a
   directory handle per level. 0, or -1 on a host failure. */
static int remove_tree(char *path, size_t size)
{
    DIR *dir = opendir(path);
    if (!dir) return -1;
    const size_t length = strlen(path);
    int result = 0;
    for (struct dirent *entry; !result && (entry = readdir(dir));) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (length + 1 + strlen(entry->d_name) >= size) { result = -1; break; }
        path[length] = '/';
        strcpy(path + length + 1, entry->d_name);
        struct stat st;
        if (lstat(path, &st)) result = -1;
        else if (S_ISDIR(st.st_mode)) result = remove_tree(path, size);
        else result = unlink(path);
        path[length] = 0;
    }
    closedir(dir);
    return result || rmdir(path) ? -1 : 0;
}

static int32_t delete_save(const delete_request *request)
{
    static const char fn[] = "sceSaveDataDelete";
    pthread_mutex_lock(&save_lock);
    int32_t error = !initialized ? not_initialized() : !request ? SAVE_ERROR_PARAMETER : 0;
    if (!error) error = check_user(fn, request->user);
    if (!error && !valid_dir_name(request->dir_name)) error = SAVE_ERROR_PARAMETER;
    if (error) goto done;
    if (request->title_id) rt_fatal(fn, "another title's saves (a title id) are not modelled");
    for (int slot = 0; slot < MAX_MOUNTS; slot++)
        if (mounts[slot].used && !strcmp(mounts[slot].dir_name, request->dir_name)) error = SAVE_ERROR_BUSY;
    if (error) goto done;
    static char save_dir[4096], sce_sys[4200];
    user_path(fn, request->dir_name, save_dir, sizeof save_dir);
    path_printf(fn, sce_sys, sizeof sce_sys, "%s/sce_sys", save_dir);
    if (!path_exists(sce_sys)) rt_fatal(fn, "a save that is not there: the answer is not established");
    if (remove_tree(save_dir, sizeof save_dir))
        rt_fatal(fn, "the host could not remove the save's folder");
done:
    pthread_mutex_unlock(&save_lock);
    return error;
}

/* The slot a mount point string names, or -1; with save_lock held. */
static int slot_of(const char *mount_point)
{
    for (int slot = 0; mount_point && slot < MAX_MOUNTS; slot++) {
        char point[RT_MOUNT_POINT_BYTES];
        snprintf(point, sizeof point, "/savedata%d", slot);
        if (mounts[slot].used && !strncmp(mount_point, point, MOUNT_POINT_BYTES)) return slot;
    }
    return -1;
}

static int32_t unmount_save(const char *mount_point)
{
    pthread_mutex_lock(&save_lock);
    int32_t error = !initialized ? not_initialized() : !mount_point ? SAVE_ERROR_PARAMETER : 0;
    const int slot = error ? -1 : slot_of(mount_point);
    if (!error && slot < 0) error = SAVE_ERROR_NOT_FOUND;
    if (!error) unmount_slot(slot);
    pthread_mutex_unlock(&save_lock);
    return error;
}

static int32_t set_param(const char *mount_point, uint32_t type, const void *buffer, size_t size)
{
    static const char fn[] = "sceSaveDataSetParam";
    pthread_mutex_lock(&save_lock);
    int32_t error = !initialized ? not_initialized() : (type > PARAM_USER || !mount_point || !buffer) ? SAVE_ERROR_PARAMETER : 0;
    const int slot = error ? -1 : slot_of(mount_point);
    if (!error && slot < 0) error = SAVE_ERROR_NOT_MOUNTED;
    if (error) goto done;
    save_param param = {0};
    read_param(mounts[slot].host_dir, &param); /* a missing one starts empty */
    if (type == PARAM_ALL) {
        if (size != sizeof param) rt_fatal(fn, "a whole param of a size other than 1328 bytes");
        memcpy(&param, buffer, sizeof param);
    } else if (type == PARAM_USER) {
        if (size < sizeof param.user_param) rt_fatal(fn, "a user param shorter than 4 bytes");
        memcpy(&param.user_param, buffer, sizeof param.user_param);
    } else {
        char *field = type == PARAM_TITLE ? param.title : type == PARAM_SUBTITLE ? param.subtitle : param.detail;
        const size_t room = type == PARAM_DETAIL ? sizeof param.detail : sizeof param.title;
        const size_t n = strnlen(buffer, size < room - 1 ? size : room - 1);
        memcpy(field, buffer, n);
        field[n] = 0;
    }
    param.mtime = (int64_t)time(NULL);
    /* a read-only mount keeps the change only while mounted on a console; here it is not written */
    if (!mounts[slot].read_only && write_param(mounts[slot].host_dir, &param)) rt_fatal(fn, "the host could not write the param");
done:
    pthread_mutex_unlock(&save_lock);
    return error;
}

static int32_t save_icon_file(const char *mount_point, const save_icon *icon)
{
    static const char fn[] = "sceSaveDataSaveIcon";
    pthread_mutex_lock(&save_lock);
    int32_t error = !initialized ? not_initialized() : (!mount_point || !icon || !icon->buffer) ? SAVE_ERROR_PARAMETER : 0;
    const int slot = error ? -1 : slot_of(mount_point);
    if (!error && slot < 0) error = SAVE_ERROR_NOT_MOUNTED;
    if (!error && mounts[slot].read_only) error = SAVE_ERROR_BAD_MOUNTED;
    if (!error) {
        static char path[4300];
        path_printf(fn, path, sizeof path, "%s/sce_sys/icon0.png", mounts[slot].host_dir);
        const uint64_t n = icon->buffer_size < icon->data_size ? icon->buffer_size : icon->data_size;
        if (write_file(path, icon->buffer, (size_t)n)) rt_fatal(fn, "the host could not write the icon");
    }
    pthread_mutex_unlock(&save_lock);
    return error;
}

/* SQL LIKE, case-insensitive: '%' any run of characters, '_' one character. */
static int like(const char *pattern, const char *text)
{
    if (!*pattern) return !*text;
    if (*pattern == '%') return like(pattern + 1, text) || (*text && like(pattern, text + 1));
    if (!*text) return 0;
    if (*pattern != '_' && tolower((unsigned char)*pattern) != tolower((unsigned char)*text)) return 0;
    return like(pattern + 1, text + 1);
}

typedef struct {
    char name[DIR_NAME_BYTES];
    save_param param;
} found_save;

static uint32_t sort_key, sort_order;

static int compare_saves(const void *a, const void *b)
{
    const found_save *x = a, *y = b;
    int c = 0;
    if (sort_key == SORT_USER_PARAM) c = (x->param.user_param > y->param.user_param) - (x->param.user_param < y->param.user_param);
    else if (sort_key == SORT_MTIME) c = (x->param.mtime > y->param.mtime) - (x->param.mtime < y->param.mtime);
    if (!c) c = strcmp(x->name, y->name); /* equal keys (and blocks, all alike here) by name */
    return sort_order == ORDER_DESCENT ? -c : c;
}

static int32_t dir_name_search(const search_condition *condition, search_result *result)
{
    static const char fn[] = "sceSaveDataDirNameSearch";
    enum { MAX_FOUND = 1024 };
    pthread_mutex_lock(&save_lock);
    int32_t error = !initialized ? not_initialized() : (!condition || !result) ? SAVE_ERROR_PARAMETER : 0;
    if (!error && (condition->key > SORT_FREE_BLOCKS || condition->order > ORDER_DESCENT)) error = SAVE_ERROR_PARAMETER;
    if (!error) error = check_user(fn, condition->user);
    if (error) goto done;
    if (condition->title_id) rt_fatal(fn, "another title's saves (a title id) are not modelled");
    if (condition->key == 4) rt_fatal(fn, "sort key 4 is not established");
    if (rt_compiled_sdk_version() >= SDK_2_50 && result->infos) rt_fatal(fn, "block counts (SDK 2.50 and later) are not modelled");
    static found_save found[MAX_FOUND];
    uint32_t total = 0;
    static char user_dir[4096];
    user_path(fn, NULL, user_dir, sizeof user_dir);
    DIR *dir = opendir(user_dir);
    for (struct dirent *e; dir && (e = readdir(dir));) {
        if (!valid_dir_name(e->d_name) || strlen(e->d_name) >= DIR_NAME_BYTES || !strncmp(e->d_name, "sce_", 4)) continue;
        if (condition->dir_name && condition->dir_name[0] && !like(condition->dir_name, e->d_name)) continue;
        static char save_dir[4400];
        path_printf(fn, save_dir, sizeof save_dir, "%s/%s", user_dir, e->d_name);
        found_save entry = {0};
        if (read_param(save_dir, &entry.param)) continue; /* no sce_sys/param.bin: not a save */
        if (total == MAX_FOUND) rt_fatal(fn, "more saves than the runtime lists");
        path_printf(fn, entry.name, sizeof entry.name, "%s", e->d_name);
        found[total++] = entry;
    }
    if (dir) closedir(dir);
    sort_key = condition->key, sort_order = condition->order;
    qsort(found, total, sizeof *found, compare_saves);
    const uint32_t set = total < result->dir_names_capacity ? total : result->dir_names_capacity;
    if (set && !result->dir_names) rt_fatal(fn, "no array for the directory names");
    for (uint32_t i = 0; i < set; i++) {
        memcpy(result->dir_names[i], found[i].name, DIR_NAME_BYTES);
        if (result->params && rt_compiled_sdk_version() >= SDK_1_70) result->params[i] = found[i].param;
    }
    if (rt_compiled_sdk_version() >= SDK_1_70) {
        result->hit_count = total;
        result->set_count = set;
    } else {
        result->hit_count = set;
    }
done:
    pthread_mutex_unlock(&save_lock);
    return error;
}

/* The memory-backed save: one region per user, set up with its size, then read and written by
   offset. */
static void memory_path(const char *caller, const char *leaf, char *out, size_t size)
{
    char relative[64];
    path_printf(caller, relative, sizeof relative, "sce_sdmemory%s%s", leaf ? "/" : "", leaf ? leaf : "");
    user_path(caller, relative, out, size);
}

static int32_t setup_memory(int32_t user, uint64_t size, const save_param *param)
{
    static const char fn[] = "sceSaveDataSetupSaveDataMemory";
    pthread_mutex_lock(&save_lock);
    int32_t error = !initialized ? not_initialized() : !size ? SAVE_ERROR_PARAMETER : 0;
    if (!error) error = check_user(fn, user);
    if (!error) {
        static char dir[4096], file[4200];
        memory_path(fn, NULL, dir, sizeof dir);
        memory_path(fn, "memory.dat", file, sizeof file);
        if (make_directories(dir)) rt_fatal(fn, "the host could not create the memory save's directory");
        const int h = open(file, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        struct stat st;
        if (h < 0 || fstat(h, &st)) rt_fatal(fn, "the host could not open the memory save");
        if ((uint64_t)st.st_size < size && ftruncate(h, (off_t)size)) rt_fatal(fn, "the host could not size the memory save");
        close(h); /* an existing larger one is kept whole */
        save_param initial = {0};
        if (param) initial = *param;
        if ((param || read_param(dir, &initial)) && write_param(dir, &initial)) rt_fatal(fn, "the host could not write the param");
        memory_size = size;
    }
    pthread_mutex_unlock(&save_lock);
    return error;
}

static int32_t memory_io(const char *caller, int32_t user, void *buffer, uint64_t size, int64_t offset, int writing)
{
    pthread_mutex_lock(&save_lock);
    int32_t error = !initialized ? not_initialized() : 0;
    if (!error) error = check_user(caller, user);
    if (!error && !memory_size) error = SAVE_ERROR_MEMORY_NOT_READY;
    if (!error && (!buffer || offset < 0)) error = SAVE_ERROR_PARAMETER;
    if (error) goto done;
    if ((uint64_t)offset > memory_size || size > memory_size - (uint64_t)offset)
        rt_fatal(caller, "a read or write past the size set up: the answer is not established");
    static char file[4200];
    memory_path(caller, "memory.dat", file, sizeof file);
    const int h = open(file, writing ? O_WRONLY | O_CLOEXEC : O_RDONLY | O_CLOEXEC);
    if (h < 0) rt_fatal(caller, "the host could not open the memory save");
    const ssize_t n = writing ? pwrite(h, buffer, (size_t)size, (off_t)offset) : pread(h, buffer, (size_t)size, (off_t)offset);
    close(h);
    if (n < 0 || (uint64_t)n != size) rt_fatal(caller, "the host read or wrote less than asked");
done:
    pthread_mutex_unlock(&save_lock);
    return error;
}

static int32_t get_memory(int32_t user, void *buffer, uint64_t size, int64_t offset)
{
    return memory_io("sceSaveDataGetSaveDataMemory", user, buffer, size, offset, 0);
}

static int32_t set_memory(int32_t user, const void *buffer, uint64_t size, int64_t offset)
{
    return memory_io("sceSaveDataSetSaveDataMemory", user, (void *)buffer, size, offset, 1);
}

RT_SYSLIB("libSceSaveData", sceSaveDataInitialize, initialize);
RT_SYSLIB("libSceSaveData", sceSaveDataTerminate, terminate);
RT_SYSLIB("libSceSaveData", sceSaveDataMount, mount_save);
RT_SYSLIB("libSceSaveData", sceSaveDataUmount, unmount_save);
RT_SYSLIB("libSceSaveData", sceSaveDataSetParam, set_param);
RT_SYSLIB("libSceSaveData", sceSaveDataSaveIcon, save_icon_file);
RT_SYSLIB("libSceSaveData", sceSaveDataDirNameSearch, dir_name_search);
RT_SYSLIB("libSceSaveData", sceSaveDataSetupSaveDataMemory, setup_memory);
RT_SYSLIB("libSceSaveData", sceSaveDataGetSaveDataMemory, get_memory);
RT_SYSLIB("libSceSaveData", sceSaveDataSetSaveDataMemory, set_memory);
RT_SYSLIB("libSceSaveData", sceSaveDataDelete, delete_save);
