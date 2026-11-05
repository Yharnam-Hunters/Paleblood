/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The shader translator (runtime/renderer/gcn_spirv.c) without a GPU: each test program becomes a
   SPIR-V module with the right header, which spirv-val (when installed) accepts for Vulkan 1.2;
   an instruction not handled yet stops translation and is named. test_gcn_compute runs the
   modules on a GPU. */
#include "runtime/gcn_spirv.h"

#include "gcn_programs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define PROGRAM(code) code, sizeof code / sizeof code[0]

static const gcn_compute_layout layout = {{64, 1, 1}, 8, -1};

/* Runs spirv-val on the module: 1 valid, 0 invalid, -1 when spirv-val is not installed. */
static int validate(const uint32_t *spirv, size_t words, const char *name)
{
    if (system("spirv-val --version > /dev/null 2>&1") != 0) return -1;
    char path[512];
    const char *tmp = getenv("TMPDIR");
    snprintf(path, sizeof path, "%s/pb-gcn-%s.spv", tmp ? tmp : "/tmp", name);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(spirv, sizeof *spirv, words, f);
    fclose(f);
    char command[700];
    snprintf(command, sizeof command, "spirv-val --target-env vulkan1.2 %s", path);
    const int ok = system(command) == 0;
    remove(path);
    return ok;
}

static void translates(const uint32_t *code, size_t dwords, const char *name)
{
    uint32_t *spirv = NULL;
    size_t words = 0;
    char error[256];
    const int result = gcn_translate_compute(code, dwords, &layout, &spirv, &words, error, sizeof error);
    if (result) fprintf(stderr, "  %s: %s\n", name, error);
    CHECK(result == 0 && spirv && words > 5);
    if (!spirv) return;
    CHECK(spirv[0] == 0x07230203u && spirv[1] == 0x00010500u); /* magic, SPIR-V 1.5 */
    const int valid = validate(spirv, words, name);
    if (valid < 0) fprintf(stderr, "  %s: spirv-val not installed; validation skipped\n", name);
    else CHECK(valid == 1);
    free(spirv);
}

int main(void)
{
    translates(PROGRAM(scale_add), "scale_add");
    translates(PROGRAM(integer_and_select), "integer_and_select");
    translates(PROGRAM(constants_and_modifiers), "constants_and_modifiers");
    translates(PROGRAM(divergent_if), "divergent_if");
    translates(PROGRAM(uniform_loop), "uniform_loop");
    translates(PROGRAM(vector_loop), "vector_loop");
    translates(PROGRAM(compare_exec), "compare_exec");
    translates(PROGRAM(masked_store), "masked_store");

    uint32_t *spirv = NULL;
    size_t words = 0;
    char error[256] = "";
    CHECK(gcn_translate_compute(PROGRAM(unsupported), &layout, &spirv, &words, error, sizeof error) == -1);
    CHECK(spirv == NULL && strstr(error, "v_interp_p1_f32") != NULL);

    const uint32_t no_end[] = {0x7e020302}; /* v_mov_b32 v1, v2, and no s_endpgm */
    CHECK(gcn_translate_compute(no_end, 1, &layout, &spirv, &words, error, sizeof error) == -1);

    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    return failures != 0;
}
