/* SPDX-License-Identifier: GPL-2.0-or-later */
/* sceSystemServiceLaunchWebBrowser (runtime/syslib/system_service.c): no browser is opened; a URL
   answers 0, and no URL stops by name. */
#include "../syslib_test.h"

#include <stdint.h>

typedef int32_t (*launch_fn)(const char *, const void *);

static launch_fn launch;

static void launch_without_url(void) { launch(NULL, NULL); }

SYSLIB_TEST(system_service_web_browser)
{
    launch = (launch_fn)fn("libSceSystemService", "sceSystemServiceLaunchWebBrowser");
    CHECK(is_implemented("libSceSystemService", "sceSystemServiceLaunchWebBrowser"));
    if (!launch) return;
    CHECK(launch("http://example.com/", NULL) == 0);
    CHECK(died_in_fatal(launch_without_url));
}
