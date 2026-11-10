// SPDX-License-Identifier: GPL-2.0-or-later
// Empty functions: every member of the returns_nothing family (symbols/families.csv) is a lone
// `ret`, the compiler's body for an empty virtual method or a callback that does nothing.
// The original leaves every register and all memory as it found them, so the replacement must
// too; the family's cases check rax and the argument registers as well as memory
// (game/leaf/family_cases.py).

extern "C" void bb_leaf_returns_nothing(void)
{
}
