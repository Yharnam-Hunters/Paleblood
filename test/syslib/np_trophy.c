/* SPDX-License-Identifier: GPL-2.0-or-later */
/* libSceNpTrophy offline (runtime/syslib/np_trophy.c): contexts, handles, registration, and unlocks
   recorded by id in <savedata>/<user>/sce_trophy. */
#include "../syslib_test.h"

#include "runtime/process.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef int32_t (*f_tr_context)(int32_t *, int32_t, uint32_t, uint64_t);
typedef int32_t (*f_tr_handle)(int32_t *);
typedef int32_t (*f_tr_register)(int32_t, int32_t, uint64_t);
typedef int32_t (*f_tr_unlock)(int32_t, int32_t, int32_t, int32_t *);
typedef int32_t (*f_tr_game)(int32_t, int32_t, void *, void *);
typedef int32_t (*f_tr_info)(int32_t, int32_t, int32_t, void *, void *);
typedef struct { uint64_t size; uint32_t groups, trophies, platinum, gold, silver, bronze; char title[128], description[1024]; } tr_game_details;
typedef struct { uint64_t size; uint32_t unlocked, platinum, gold, silver, bronze, progress; } tr_game_data;
typedef struct { uint64_t size; int32_t id, grade, group; uint8_t hidden, reserved[3]; char name[128], description[1024]; } tr_details;
typedef struct { uint64_t size; int32_t id; uint8_t unlocked, reserved[3]; uint64_t tick; } tr_data;

SYSLIB_TEST(np_trophy)
{
    const f_tr_context tr_context = (f_tr_context)fn("libSceNpTrophy", "sceNpTrophyCreateContext");
    const f_tr_handle tr_handle = (f_tr_handle)fn("libSceNpTrophy", "sceNpTrophyCreateHandle");
    const f_tr_register tr_register = (f_tr_register)fn("libSceNpTrophy", "sceNpTrophyRegisterContext");
    const f_tr_unlock tr_unlock = (f_tr_unlock)fn("libSceNpTrophy", "sceNpTrophyUnlockTrophy");
    const f_tr_game tr_game = (f_tr_game)fn("libSceNpTrophy", "sceNpTrophyGetGameInfo");
    const f_tr_info tr_info = (f_tr_info)fn("libSceNpTrophy", "sceNpTrophyGetTrophyInfo");
    CHECK(tr_context && tr_handle && tr_register && tr_unlock && tr_game && tr_info);
    static char trophy_saves[512];
    const char *tmp = getenv("TMPDIR");
    snprintf(trophy_saves, sizeof trophy_saves, "%s/pb-trophies-XXXXXX", tmp ? tmp : "/tmp");
    CHECK(mkdtemp(trophy_saves) != NULL);
    rt_process.savedata_dir = trophy_saves;
    if (tr_context && tr_handle && tr_register && tr_unlock && tr_game && tr_info) {
        int32_t ctx = 0, ctx2 = 0, handle = 0, platinum = 7;
        CHECK(tr_context(NULL, 1, 0, 0) == (int32_t)0x80551604u && tr_context(&ctx, 1, 0, 1) == (int32_t)0x80551604u);
        CHECK(tr_context(&ctx, 1, 0, 0) == 0 && ctx == 1);
        CHECK(tr_context(&ctx2, 1, 0, 0) == (int32_t)0x80551613u);          /* same user and label */
        for (uint32_t label = 1; label < 8; label++) CHECK(tr_context(&ctx2, 1, label, 0) == 0);
        CHECK(tr_context(&ctx2, 1, 8, 0) == (int32_t)0x80551623u);          /* the ninth */
        CHECK(tr_handle(&handle) == 0 && handle == 1);
        int32_t extra;
        for (int k = 0; k < 3; k++) CHECK(tr_handle(&extra) == 0);
        CHECK(tr_handle(&extra) == (int32_t)0x80551624u && tr_handle(NULL) == (int32_t)0x80551604u);
        CHECK(tr_unlock(ctx, handle, 3, &platinum) == (int32_t)0x8055160Fu); /* not registered */
        CHECK(tr_register(9, handle, 0) == (int32_t)0x80551609u && tr_register(ctx, 9, 0) == (int32_t)0x80551608u);
        CHECK(tr_register(ctx, handle, 1) == (int32_t)0x80551604u);
        CHECK(tr_register(ctx, handle, 0) == 0 && tr_register(ctx, handle, 0) == (int32_t)0x80551610u);
        CHECK(tr_unlock(ctx, handle, 128, &platinum) == (int32_t)0x8055160Au && tr_unlock(ctx, handle, 3, NULL) == (int32_t)0x80551604u);
        const time_t before = time(NULL);
        CHECK(tr_unlock(ctx, handle, 3, &platinum) == 0 && platinum == -1);
        CHECK(tr_unlock(ctx, handle, 3, &platinum) == (int32_t)0x8055160Cu);
        tr_details details = {.size = 0x498};
        tr_data data = {.size = 0x18};
        CHECK(tr_info(ctx, handle, 3, &details, &data) == 0 && data.id == 3 && data.unlocked == 1 && details.group == -1);
        CHECK(data.tick / 1000000 >= 62135596800ull + (uint64_t)before && data.tick / 1000000 <= 62135596800ull + (uint64_t)before + 5);
        CHECK(tr_info(ctx, handle, 4, &details, &data) == 0 && data.unlocked == 0 && data.tick == 0);
        data.size = 0x20;
        CHECK(tr_info(ctx, handle, 4, &details, &data) == (int32_t)0x80551604u);
        tr_game_details game = {.size = 0x4A0};
        tr_game_data progress = {.size = 0x20};
        CHECK(tr_game(ctx, handle, &game, &progress) == 0 && progress.unlocked == 1 && game.trophies == 0);
        char record[700];
        snprintf(record, sizeof record, "%s/1/sce_trophy/unlocked", trophy_saves);
        FILE *f = fopen(record, "r");
        int recorded_id = -1;
        CHECK(f && fscanf(f, "%d", &recorded_id) == 1 && recorded_id == 3);
        if (f) fclose(f);
    }
    rt_process.savedata_dir = NULL;
}
