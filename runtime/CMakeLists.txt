# SPDX-License-Identifier: GPL-2.0-or-later
add_library(bbrt STATIC hooks.c plugin.c capture.c probe.c loader.c)
target_include_directories(bbrt PUBLIC include)
# The executable loader on its own, for the verification harness (tools/harness.py loads it).
add_library(pbloader SHARED loader.c)
target_include_directories(pbloader PUBLIC include)
