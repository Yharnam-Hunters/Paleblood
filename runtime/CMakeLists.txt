# SPDX-License-Identifier: GPL-2.0-or-later
add_library(bbrt STATIC hooks.c plugin.c capture.c probe.c)
target_include_directories(bbrt PUBLIC include)
