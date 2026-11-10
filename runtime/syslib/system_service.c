/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: system services and dialogs. The console's system settings
   (libSceSystemService).

   Facts used: int32_t sceSystemServiceParamGetInt(int32_t id, int32_t *value), with id 1 the
   language (the OpenOrbis homebrew SDK's SystemService.h and _types/sys_service.h, which also list
   the language codes). Every call in the executable (nine sites) asks for id 1, into a variable it
   preset to -1.

   A setting is the console owner's choice, so the runtime answers what the person running it chose
   (rt_process.system_params, from tools/boot.py, which defaults the language to English (United
   States)). A setting nobody gave, and a missing variable, stop by name. */
#include "runtime/process.h"
#include "runtime/system_service.h"
#include "runtime/syslib.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t param_get_int(int32_t id, int32_t *value)
{
    static const char fn[] = "sceSystemServiceParamGetInt";
    if (!value) rt_fatal(fn, "no variable for the value: the error value is not established");
    for (uint32_t i = 0; i < rt_process.system_param_count; i++)
        if (rt_process.system_params[i].id == id) {
            *value = rt_process.system_params[i].value;
            return 0;
        }
    char message[128];
    snprintf(message, sizeof message, "setting %d: no value given (tools/boot.py --system-param)", (int)id);
    rt_fatal(fn, message);
}

RT_SYSLIB("libSceSystemService", sceSystemServiceParamGetInt, param_get_int);

/* sceSystemServiceHideSplashScreen(void): the title tells the system it may take down the boot
   splash, because the title's own screens follow; it answers 0 (shadPS4's libSceSystemService,
   systemservice.cpp). The executable calls it through a function pointer (0x01d5d995) and reads no
   result. The runtime shows no splash; it keeps the fact (rt_splash_hidden), a sign of how far a
   boot got. */
static atomic_int splash_hidden;

int rt_splash_hidden(void) { return atomic_load(&splash_hidden); }

static int32_t hide_splash_screen(void)
{
    atomic_store(&splash_hidden, 1);
    return 0;
}
RT_SYSLIB("libSceSystemService", sceSystemServiceHideSplashScreen, hide_splash_screen);

/* sceSystemServiceGetStatus(status *): the system's state for the title, polled for events:
   { int32_t pending system events, then one byte each: system UI overlaid, in background
   execution, CPU mode 7 normal, live streaming on air, out of the VR play area }; a missing
   pointer answers 0x80A10003 (shadPS4's libSceSystemService). The runtime has no system UI and no
   system events, so: no events, nothing overlaid, in the foreground, CPU mode normal (as shadPS4
   answers), not streaming, not in VR. Only these 9 bytes are written: the reserved tail's size is
   not established. */
#define SYSTEM_SERVICE_ERROR_PARAMETER ((int32_t)0x80A10003u)

static int32_t get_status(uint8_t *status)
{
    if (!status) return SYSTEM_SERVICE_ERROR_PARAMETER;
    const int32_t pending_events = 0;
    memcpy(status, &pending_events, sizeof pending_events);
    const uint8_t flags[5] = {0, 0, 1, 0, 0}; /* overlaid, background, CPU mode 7 normal, streaming, out of VR area */
    memcpy(status + 4, flags, sizeof flags);
    return 0;
}
RT_SYSLIB("libSceSystemService", sceSystemServiceGetStatus, get_status);

/* sceSystemServiceReceiveEvent(event *): takes the oldest pending system event (shadPS4's
   libSceSystemService: a missing pointer answers 0x80A10003, an empty queue 0x80A10004, "no
   event", in its systemservice_error.h). The executable calls it only inside a loop bounded by the
   pending count sceSystemServiceGetStatus gave (0x01b8a433, 0x02276aa3), and acts on an event only
   when the call answers 0. The runtime raises no system events, so there is never one to take: it
   answers no event and writes nothing. */
#define SYSTEM_SERVICE_ERROR_NO_EVENT ((int32_t)0x80A10004u)

static int32_t receive_event(void *event)
{
    return event ? SYSTEM_SERVICE_ERROR_NO_EVENT : SYSTEM_SERVICE_ERROR_PARAMETER;
}
RT_SYSLIB("libSceSystemService", sceSystemServiceReceiveEvent, receive_event);

/* sceSystemServiceLaunchWebBrowser(url, param): asks the system to open its web browser at a
   page (OpenOrbis' SystemService.h names it without a shape; the executable's three calls pass a
   URL string and NULL). The executable discards the answer at every call: the official site link
   at 0x02437039 and its debug "BROWSER_TEST" command at 0x01d5eeeb (the same URL), and a URL it
   builds at 0x02437112. The runtime opens no browser, as if the user closed it at once: it notes
   the URL on stderr and answers 0 (shadPS4 also answers 0, facts only). No URL stops by name. */
static int32_t launch_web_browser(const char *url, const void *param)
{
    (void)param;
    if (!url) rt_fatal("sceSystemServiceLaunchWebBrowser", "no URL: the error value is not established");
    fprintf(stderr, "runtime: sceSystemServiceLaunchWebBrowser: %s (no browser opened)\n", url);
    return 0;
}

RT_SYSLIB("libSceSystemService", sceSystemServiceLaunchWebBrowser, launch_web_browser);
