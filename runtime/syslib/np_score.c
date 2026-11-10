/* SPDX-License-Identifier: GPL-2.0-or-later */
/* System call group: network and online services, offline. PSN leaderboards (libSceNpScore), on a
   console whose user is not signed in: no title context can be created, so no request exists.

   The executable's leaderboard object (constructor near 0x010d10d8) creates its title context at
   0x010ccdfe only after it has the signed-in user's NpId, which sceNpGetNpId never gives offline,
   so it keeps context 0. Yet sceNpScoreCreateRequest is reached with that 0: the comment checks
   (0x010d07a8, 0x010d0a39) and the leaderboard jobs (0x010cdf63, 0x010cf339, 0x010d04ed) call it
   and treat an answer below 1 as failure.

   Facts (facts only): shadPS4's np_score.cpp answers 0x8055070E (invalid id) for a title context
   or request it did not create, and 0x80550704 (invalid argument) for a negative character id;
   PS4Recomp's libSceNpScore answers 0x80550006 (signed out) to sceNpScoreCreateNpTitleCtx, and
   shadPS4's sign-in-checking variant refuses too (its plain variant and one port hand out an id;
   the signed-out answer is used, as every other PSN call here answers signed out). The codes are
   the executable's own: its comment check maps 0x8055080B and 0x80550707.

   What the runtime does: sceNpScoreCreateNpTitleCtx answers 0x80550006. Since no title context or
   request ever exists, every call that names one answers 0x8055070E (SetPlayerCharacterId first
   answers 0x80550704 for a negative character id). */
#include "runtime/syslib.h"

#include <stdint.h>

#define NP_ERROR_SIGNED_OUT ((int32_t)0x80550006u)
#define NP_COMMUNITY_ERROR_INVALID_ARGUMENT ((int32_t)0x80550704u)
#define NP_COMMUNITY_ERROR_INVALID_ID ((int32_t)0x8055070Eu)

static int32_t create_title_context(uint32_t service_label, const void *np_id)
{
    (void)service_label, (void)np_id;
    return NP_ERROR_SIGNED_OUT;
}

/* Any title context or request id: none exists offline. */
static int32_t no_such_id(int32_t id)
{
    (void)id;
    return NP_COMMUNITY_ERROR_INVALID_ID;
}

static int32_t set_player_character_id(int32_t id, int32_t character_id)
{
    (void)id;
    return character_id < 0 ? NP_COMMUNITY_ERROR_INVALID_ARGUMENT : NP_COMMUNITY_ERROR_INVALID_ID;
}

RT_SYSLIB("libSceNpScore", sceNpScoreCreateNpTitleCtx, create_title_context);
RT_SYSLIB("libSceNpScore", sceNpScoreCreateRequest, no_such_id);
RT_SYSLIB("libSceNpScore", sceNpScoreDeleteNpTitleCtx, no_such_id);
RT_SYSLIB("libSceNpScore", sceNpScoreDeleteRequest, no_such_id);
RT_SYSLIB("libSceNpScore", sceNpScoreAbortRequest, no_such_id);
RT_SYSLIB("libSceNpScore", sceNpScoreSetPlayerCharacterId, set_player_character_id);
RT_SYSLIB("libSceNpScore", sceNpScoreGetBoardInfo, no_such_id);
RT_SYSLIB("libSceNpScore", sceNpScoreGetGameData, no_such_id);
RT_SYSLIB("libSceNpScore", sceNpScoreGetRankingByNpIdPcId, no_such_id);
RT_SYSLIB("libSceNpScore", sceNpScoreGetRankingByRange, no_such_id);
RT_SYSLIB("libSceNpScore", sceNpScoreRecordGameData, no_such_id);
RT_SYSLIB("libSceNpScore", sceNpScoreRecordScore, no_such_id);
RT_SYSLIB("libSceNpScore", sceNpScoreCensorComment, no_such_id);
RT_SYSLIB("libSceNpScore", sceNpScoreSanitizeComment, no_such_id);
