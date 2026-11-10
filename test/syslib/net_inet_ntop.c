/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sceNetInetNtop (runtime/syslib/network.c): an IPv4 address in network order as dotted-decimal
   text, the destination answered; other families, missing pointers and a short buffer stop. */
#include "../syslib_test.h"

#include <stdint.h>
#include <string.h>

typedef const char *(*f_ntop)(int, const void *, char *, uint32_t);
static f_ntop ntop;

static void ntop_ipv6(void) { char text[64]; const uint8_t a[16] = {0}; ntop(28, a, text, sizeof text); }
static void ntop_no_source(void) { char text[16]; ntop(2, NULL, text, sizeof text); }
static void ntop_no_destination(void) { const uint8_t a[4] = {1, 2, 3, 4}; ntop(2, a, NULL, 16); }
static void ntop_short(void) { char text[16]; const uint8_t a[4] = {10, 0, 0, 1}; ntop(2, a, text, 8); }

SYSLIB_TEST(net_inet_ntop)
{
    ntop = (f_ntop)fn("libSceNet", "sceNetInetNtop");
    CHECK(is_implemented("libSceNet", "sceNetInetNtop"));
    if (!ntop) return;
    char text[16];
    memset(text, 'x', sizeof text);
    const uint8_t widest[4] = {255, 255, 255, 255}, local[4] = {127, 0, 0, 1}, net[4] = {10, 0, 0, 1};
    CHECK(ntop(2, widest, text, sizeof text) == text && !strcmp(text, "255.255.255.255"));
    CHECK(ntop(2, local, text, sizeof text) == text && !strcmp(text, "127.0.0.1"));
    char exact[9];
    CHECK(ntop(2, net, exact, sizeof exact) == exact && !strcmp(exact, "10.0.0.1"));
    CHECK(died_in_fatal(ntop_ipv6) && died_in_fatal(ntop_no_source) && died_in_fatal(ntop_no_destination));
    CHECK(died_in_fatal(ntop_short));
}
