/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Linear copies: runtime/linear_copy.h. */

#include "runtime/linear_copy.h"

#include "runtime/gcn.h"

#include <string.h>

/* MUBUF fields (AMD's Sea Islands instruction set manual, MUBUF): OFFEN, IDXEN, ADDR64 and LDS in the
   first word with the 12-bit offset; VADDR, VDATA, SRSRC (in fours), TFE and SOFFSET in the second. */
enum {
    MUBUF_OFFEN = 1u << 12, MUBUF_IDXEN = 1u << 13, MUBUF_ADDR64 = 1u << 15, MUBUF_LDS = 1u << 16,
    MUBUF_OFFSET_MASK = 0xfff, MUBUF_TFE = 1u << 23, SOFFSET_ZERO = 128
};
/* The bits Sea Islands leaves reserved in the two words (later generations give some a meaning). */
enum { MUBUF_RESERVED_0 = 1u << 17 | 1u << 25, MUBUF_RESERVED_1 = 1u << 21 };

static int indexed_only(const uint32_t *w)
{
    const uint32_t first_word_fields =
        MUBUF_OFFEN | MUBUF_IDXEN | MUBUF_ADDR64 | MUBUF_LDS | MUBUF_OFFSET_MASK | MUBUF_RESERVED_0;
    return (w[0] & first_word_fields) == MUBUF_IDXEN && !(w[1] & (MUBUF_TFE | MUBUF_RESERVED_1)) &&
           (w[1] >> 24) == SOFFSET_ZERO;
}

/* s_waitcnt's vmcnt (AMD's Sea Islands instruction set manual, SOPP: SIMM16 bits 3:0). */
enum { VMCNT_MASK = 0xf };

/* Whether SGPR n lies in first..first + count - 1. */
static int within(unsigned n, unsigned first, unsigned count) { return n >= first && n < first + count; }

int gcn_linear_copy_shape(const uint32_t *code, size_t dwords, gcn_linear_copy *shape)
{
    /* Scalar operands 0-103 are the ordinary SGPRs; above them lie FLAT_SCRATCH, VCC (106-107), the trap
       registers, M0 (124) and EXEC (126-127) (AMD's Sea Islands instruction set manual, the scalar operand
       fields). A write to one of those could change which lanes store, so only the add's and the compare's
       VCC and the s_and_saveexec_b64's EXEC are allowed, where they stand, and an s_mov_b32 into VCC's halves
       before the compare: VCC's one reader, the exec save, needs the compare, which rewrites VCC first. */
    enum {
        NONE = ~0u, SGPRS = 104, VCC_LOW = 106, INLINE_ZERO = 128, SHIFT_MOST = 16, LANE_ID = 0,
        DESCRIPTOR_SGPRS = 4, MOST_WRITTEN = 8
    };
    const long program = gcn_program_dwords(code, dwords);
    if (program < 0) return 0;
    gcn_linear_copy found = {.source_sgpr = NONE, .destination_sgpr = NONE, .workgroup_sgpr = NONE};
    unsigned scaled = NONE, index = NONE, loaded = NONE, guard_records = NONE;
    struct {
        unsigned sgpr;
        int stage; /* 0 before the s_lshl_b32, 1 before the v_add_i32, 2 after it */
    } written[MOST_WRITTEN]; /* the SGPRs s_mov_b32 and s_and_saveexec_b64 write */
    unsigned written_count = 0;
    int stored = 0, guarded = 0, compared = 0, waited = 0; /* waited: an s_waitcnt vmcnt(0) since the load */
    for (size_t at = 0; at < (size_t)program;) {
        gcn_instruction ins;
        if (gcn_decode(code + at, (size_t)program - at, &ins) || !ins.name) return 0;
        const uint32_t *w = code + at;
        const char *name = ins.name;
        const int stage = (scaled != NONE) + (index != NONE);
        if (stored && strcmp(name, "s_waitcnt") && strcmp(name, "s_nop") && strcmp(name, "s_endpgm"))
            return 0; /* nothing after the store */
        if (ins.encoding == GCN_SOPP) {
            if (strcmp(name, "s_waitcnt") && strcmp(name, "s_endpgm") && strcmp(name, "s_nop")) return 0;
            if (!strcmp(name, "s_waitcnt") && loaded != NONE && !(w[0] & VMCNT_MASK)) waited = 1;
        } else if (ins.encoding == GCN_SOP1) {
            const unsigned sdst = (w[0] >> 16) & 0x7f, ssrc0 = w[0] & 0xff;
            if (!strcmp(name, "s_mov_b32")) {
                /* Only ordinary SGPR writes and VCC writes before any guard compare are harmless.
                   EXEC controls which lanes load and store; the compare's VCC must reach its guard. */
                const int vcc = sdst == VCC_LOW || sdst == VCC_LOW + 1;
                if (sdst >= SGPRS && (!vcc || compared)) return 0;
                if (written_count == MOST_WRITTEN) return 0;
                written[written_count].sgpr = sdst, written[written_count++].stage = stage;
            } else if (!strcmp(name, "s_and_saveexec_b64") && ssrc0 == VCC_LOW && compared && !guarded &&
                       !(sdst & 1) && sdst + 1 < SGPRS) { /* a 64-bit destination is an even SGPR pair */
                if (written_count + 2 > MOST_WRITTEN) return 0;
                for (unsigned k = 0; k < 2; k++)
                    written[written_count].sgpr = sdst + k, written[written_count++].stage = stage;
                guarded = 1;
            } else {
                return 0;
            }
        } else if (ins.encoding == GCN_SOP2) {
            const unsigned ssrc0 = w[0] & 0xff, ssrc1 = (w[0] >> 8) & 0xff, sdst = (w[0] >> 16) & 0x7f;
            if (strcmp(name, "s_lshl_b32") || scaled != NONE || ssrc0 >= SGPRS || sdst >= SGPRS ||
                ssrc1 < INLINE_ZERO || ssrc1 > INLINE_ZERO + SHIFT_MOST)
                return 0;
            found.workgroup_sgpr = ssrc0, found.group_elements = 1u << (ssrc1 - INLINE_ZERO), scaled = sdst;
        } else if (ins.encoding == GCN_VOP2) {
            const unsigned src0 = w[0] & 0x1ff, vsrc1 = (w[0] >> 9) & 0xff, vdst = (w[0] >> 17) & 0xff;
            if (strcmp(name, "v_add_i32") || scaled == NONE || src0 != scaled || vsrc1 != LANE_ID || index != NONE)
                return 0;
            index = vdst;
        } else if (ins.encoding == GCN_VOPC) {
            const unsigned src0 = w[0] & 0x1ff, vsrc1 = (w[0] >> 9) & 0xff;
            if (strcmp(name, "v_cmp_gt_u32") || compared || index == NONE || vsrc1 != index || src0 >= SGPRS)
                return 0;
            guard_records = src0, compared = 1;
        } else if (ins.encoding == GCN_MUBUF) {
            const unsigned vaddr = w[1] & 0xff, vdata = (w[1] >> 8) & 0xff, srsrc = ((w[1] >> 16) & 0x1f) * 4;
            const int format_load = !strcmp(name, "buffer_load_format_x");
            const int dword_load = !strcmp(name, "buffer_load_dword");
            const int format_store = !strcmp(name, "buffer_store_format_x");
            const int dword_store = !strcmp(name, "buffer_store_dword");
            if (!indexed_only(w) || index == NONE || vaddr != index || srsrc + DESCRIPTOR_SGPRS > SGPRS)
                return 0;
            if ((format_load || dword_load) && loaded == NONE) {
                if (vdata == index) return 0; /* the index stays for the store */
                loaded = vdata, found.source_sgpr = srsrc, found.format = format_load;
            } else if ((format_store || dword_store) && loaded != NONE && waited && vdata == loaded &&
                       format_store == found.format) {
                found.destination_sgpr = srsrc, stored = 1;
            } else {
                return 0;
            }
        } else {
            return 0;
        }
        at += ins.dwords;
    }
    if (!stored || compared != guarded) return 0;
    if (compared && guard_records != found.destination_sgpr + 2)
        return 0; /* the guard is the destination's record count */
    /* the descriptors stay as the dispatch set them, the workgroup index until the s_lshl_b32 reads it,
       and its scaled copy until the v_add_i32 does */
    if (within(scaled, found.source_sgpr, DESCRIPTOR_SGPRS) || within(scaled, found.destination_sgpr, DESCRIPTOR_SGPRS))
        return 0;
    for (unsigned i = 0; i < written_count; i++) {
        const unsigned sgpr = written[i].sgpr;
        if (within(sgpr, found.source_sgpr, DESCRIPTOR_SGPRS) ||
            within(sgpr, found.destination_sgpr, DESCRIPTOR_SGPRS) ||
            (sgpr == found.workgroup_sgpr && written[i].stage == 0) || (sgpr == scaled && written[i].stage == 1))
            return 0;
    }
    *shape = found;
    return 1;
}
