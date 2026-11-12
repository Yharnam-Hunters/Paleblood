// SPDX-License-Identifier: GPL-2.0-or-later
/* Preloaded into pbboot by test/test_boot_exit.py: a library whose teardown never finishes, as a
   library whose lock a stopped guest thread still holds would. Our own fixture code. If the
   process runs library destructors after its report, this one says so on stderr and then blocks
   well past the test's deadline. */
#include <stdio.h>
#include <unistd.h>

__attribute__((destructor)) static void teardown_blocks(void)
{
    fputs("boot-exit-fixture: library destructor ran\n", stderr);
    sleep(60);
}
