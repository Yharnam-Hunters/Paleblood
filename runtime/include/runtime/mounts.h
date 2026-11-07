/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Mounts made while the title runs, beside /app0 and /data (runtime/syslib/files.c): a guest
   mount point such as /savedata0 served from a host directory. The file calls resolve paths under
   them like any other mount. */
#ifndef RUNTIME_MOUNTS_H
#define RUNTIME_MOUNTS_H

#include <stddef.h>

enum { RT_MAX_MOUNTS = 16, RT_MOUNT_POINT_BYTES = 32 };

/* Serves `mount_point` (an absolute guest path without a trailing slash) from `host_dir`, read-only
   or writable. 0 on success; -1 when the point is already mounted or the table is full. */
int rt_mount_add(const char *mount_point, const char *host_dir, int read_only);

/* Removes a mount; open files on it keep working until closed. 0, or -1 when it is not mounted. */
int rt_mount_remove(const char *mount_point);

/* The host path for a guest path under /app0, /data or a mount, written to out; returns 1 when that
   mount is read-only, 0 when writable. Any other path, and one that would leave its mount, stops by
   name under caller. Uses about 4 KiB of stack. */
int rt_host_path(const char *caller, const char *guest, char *out, size_t size);

#endif
