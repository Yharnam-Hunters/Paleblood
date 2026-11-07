/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TEST_SYSLIB_TEST_H
#define TEST_SYSLIB_TEST_H

/* What the system library tests share (test/test_syslib.c defines it). A library's tests live in
   their own file under test/syslib/, as one or more SYSLIB_TEST(name) { ... } blocks: the file is
   compiled into test_syslib by the glob in CMakeLists.txt and each block runs after main's own
   tests, in name order. New tests go in a new file, so pull requests do not edit the same lines. */

#include <stdio.h>

extern int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* The registered system function library/symbol, or NULL (counted as a failure). */
void *fn(const char *library, const char *symbol);
/* Whether library/symbol is registered as implemented rather than as an unresolved stop. */
int is_implemented(const char *library, const char *symbol);
/* Runs call in a child process: true when it stopped the process by name (rt_fatal's abort). */
int died_in_fatal(void (*call)(void));

typedef struct syslib_test {
    const char *name;
    void (*run)(void);
    struct syslib_test *next;
} syslib_test;

void syslib_test_register(syslib_test *test);

#define SYSLIB_TEST(name)                                                                          \
    static void name(void);                                                                        \
    static syslib_test name##_entry = {#name, name, NULL};                                         \
    __attribute__((constructor)) static void name##_register(void) { syslib_test_register(&name##_entry); } \
    static void name(void)

#endif
