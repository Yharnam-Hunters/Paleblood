/* SPDX-License-Identifier: GPL-2.0-or-later */
/* pbgcn: decodes a GCN (GFX7) shader program from standard input, little-endian, and prints one
   line per instruction: its encoding, opcode and name ("?" for an opcode the table does not
   have). It stops at the program's end (gcn_program_dwords), before the metadata a shader binary
   carries after it.
   game/render/tools/shader_census.py runs it on the shaders of your own copy of the game. Stops at the first
   word that is no GFX7 encoding, or at an instruction cut off by the end of the input, and says
   so on standard error with exit status 1. */
#include "runtime/gcn.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    size_t capacity = 1 << 16, length = 0;
    uint32_t *code = malloc(capacity * sizeof *code);
    if (!code) return 2;
    size_t got;
    while ((got = fread(code + length, sizeof *code, capacity - length, stdin)) > 0) {
        length += got;
        if (length == capacity) {
            uint32_t *bigger = realloc(code, (capacity *= 2) * sizeof *code);
            if (!bigger) return 2;
            code = bigger;
        }
    }
    const long program = gcn_program_dwords(code, length);
    if (program == GCN_BRANCH_BEFORE_START) {
        fprintf(stderr, "pbgcn: a branch reaches before the code: the program starts earlier\n");
        return 1;
    }
    if (program < 0) {
        fprintf(stderr, "pbgcn: no program end (s_endpgm or s_setpc_b64 past every branch target) was found\n");
        return 1;
    }
    length = (size_t)program;
    for (size_t at = 0; at < length;) {
        gcn_instruction ins;
        if (gcn_decode(code + at, length - at, &ins)) {
            fprintf(stderr, "pbgcn: dword %zu (0x%08x) is no GFX7 instruction, or is cut off\n", at, code[at]);
            return 1;
        }
        printf("%s 0x%03x %s\n", gcn_encoding_name(ins.encoding), ins.opcode, ins.name ? ins.name : "?");
        at += ins.dwords;
    }
    free(code);
    return 0;
}
