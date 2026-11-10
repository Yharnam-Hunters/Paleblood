// SPDX-License-Identifier: GPL-2.0-or-later
// Float zero getters: every member of the returns_float_zero family (symbols/families.csv) is
// `vxorps xmm0, xmm0, xmm0; ret`, a function that returns 0.0f (a default weight, an unset
// speed). The whole of xmm0 is zero afterwards, which the family's cases compare. (The original's
// VEX form also clears bits 255:128 of ymm0, which the replacement's SSE code may leave; no caller
// can depend on them under the ABI.)

extern "C" float bb_leaf_returns_float_zero(void)
{
    return 0.0f;
}
