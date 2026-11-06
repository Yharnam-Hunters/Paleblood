/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Mounts made while the title runs, beside /app0 and /data (runtime/syslib/files.c): a guest
   mount point such as /savedata0 served from a host directory. The file calls resolve paths under
   them like any other mount. */
#ifndef RUNTIME_MOUNTS_H
#define RUNTIME_MOUNTS_H

enum { RT_MAX_MOUNTS = 16, RT_MOUNT_POINT_BYTES = 32 };

/* Serves `mount_point` (an absolute guest path without a trailing slash) from `host_dir`, read-only
   or writable. 0 on success; -1 when the point is already mounted or the table is full. */
int rt_mount_add(const char *mount_point, const char *host_dir, int read_only);

/* Removes a mount; open files on it keep working until closed. 0, or -1 when it is not mounted. */
int rt_mount_remove(const char *mount_point);

#endif
