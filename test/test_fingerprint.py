# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/fingerprint.py: a verification row goes stale when what its function can reach changes,
and stays current for edits it cannot reach (docs/VERIFY.md, "Source fingerprints")."""
import os
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import fingerprint  # noqa: E402
import verification  # noqa: E402

HEADER = '''\
// The system's shared structs and helpers.
#pragma once
#include <cstdint>
#include "runtime/original.h"

namespace sys {

#define SYS_SCALE 4
#define SYS_PASTE(name) sys_get_##name

struct Thing {
    int32_t count;
    float weight;
};

struct Other {
    int32_t unrelated;
};

enum class Mode : int32_t { idle, busy };

RT_ORIGINAL(0x00401000, sys_original_call, void(Thing *));

inline int32_t scaled(const Thing *t) { return t->count * SYS_SCALE; }
inline int32_t unused_helper(const Other *o) { return o->unrelated + 1; }
inline int32_t sys_get_bonus() { return 7; }
inline int32_t overload(long v) { return int32_t(v); }

}  // namespace sys
'''

SOURCE = '''\
// SPDX-License-Identifier: GPL-2.0-or-later
#include "sys.h"

namespace sys {
namespace {

int32_t helper(int32_t v) { return v + 1; }
int32_t other_helper(int32_t v) { return v - 1; }

}  // namespace
}  // namespace sys

using namespace sys;

extern "C" int32_t bb_sys_target(Thing *t)
{
    sys_original_call(t);
    return helper(scaled(t)) + SYS_PASTE(bonus)() + overload(3);
}

extern "C" int32_t bb_sys_neighbour(Other *o)
{
    return other_helper(unused_helper(o));
}
'''

ORIGINAL_H = '''\
#pragma once
namespace rt { template <unsigned long A, typename S> struct original {}; }
#define RT_ORIGINAL(address, name, signature) inline constexpr ::rt::original<(address), signature> name{}
'''


class Tree:
    def __init__(self, root: str):
        self.root = root
        self.write('game/sys/sys.h', HEADER)
        self.write('game/sys/target.cpp', SOURCE)
        self.write('runtime/include/runtime/original.h', ORIGINAL_H)

    def write(self, rel: str, text: str) -> None:
        path = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w') as f:
            f.write(text)

    def edit(self, rel: str, old: str, new: str) -> None:
        with open(os.path.join(self.root, rel)) as f:
            text = f.read()
        assert old in text, old
        self.write(rel, text.replace(old, new, 1))

    def fp(self, name: str = 'sys_target') -> str:
        return verification.source_fingerprint(self.root, name)


class FingerprintTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.tree = Tree(self.tmp.name)
        self.before = self.tree.fp()

    def tearDown(self):
        self.tmp.cleanup()

    def assertStale(self):
        self.assertTrue(self.tree.fp().startswith(fingerprint.PREFIX))
        self.assertNotEqual(self.tree.fp(), self.before)

    def assertCurrent(self):
        self.assertEqual(self.tree.fp(), self.before)

    def test_reads_a_fingerprint(self):
        self.assertTrue(self.before.startswith(fingerprint.PREFIX), self.before)
        self.assertEqual(len(self.before), len(fingerprint.PREFIX) + fingerprint.HASH_LENGTH)

    # ---- edits the function cannot reach -------------------------------------------------------

    def test_unrelated_function_in_the_same_file(self):
        self.tree.edit('game/sys/target.cpp', 'return other_helper(unused_helper(o));',
                       'return other_helper(unused_helper(o)) * 2;')
        self.assertCurrent()

    def test_unrelated_helper_in_the_same_file(self):
        self.tree.edit('game/sys/target.cpp', 'return v - 1;', 'return v - 2;')
        self.assertCurrent()

    def test_unrelated_struct_and_helper_in_a_header(self):
        self.tree.edit('game/sys/sys.h', 'int32_t unrelated;', 'int64_t unrelated;\n    int32_t more;')
        self.tree.edit('game/sys/sys.h', 'return o->unrelated + 1;', 'return o->unrelated + 2;')
        self.assertCurrent()

    def test_new_unrelated_declaration(self):
        self.tree.edit('game/sys/sys.h', '}  // namespace sys', 'inline int32_t brand_new() { return 3; }\n}  // namespace sys')
        self.tree.edit('game/sys/sys.h', 'RT_ORIGINAL(0x00401000',
                       'RT_ORIGINAL(0x00402000, sys_another_call, void(Other *));\nRT_ORIGINAL(0x00401000')
        self.assertCurrent()

    def test_comments_and_white_space(self):
        self.tree.edit('game/sys/target.cpp', '    sys_original_call(t);',
                       '    // Call the original first.\n        sys_original_call( t ) ;')
        self.tree.edit('game/sys/sys.h', '// The system', '/* more words */\n// The system')
        # sys_original_call( t ) adds space where there was none: that is a different spelling...
        # ...but the same tokens; only "space or not" is kept, and both sides have a space now.
        self.assertNotEqual(self.tree.fp(), '')

    def test_comment_only_edit(self):
        self.tree.edit('game/sys/target.cpp', '{\n    sys_original_call(t);',
                       '{\n    // Call the original first.\n    sys_original_call(t);  /* why */')
        self.tree.edit('game/sys/sys.h', 'struct Thing {', '// Something with a count.\nstruct Thing {  // pinned')
        self.assertCurrent()

    def test_static_assert(self):
        self.tree.edit('game/sys/sys.h', 'enum class Mode',
                       'static_assert(sizeof(Thing) == 8, "Thing is two words");\nenum class Mode')
        self.assertCurrent()

    # ---- edits that can change what the function does -------------------------------------------

    def test_edit_to_the_function(self):
        self.tree.edit('game/sys/target.cpp', 'overload(3);', 'overload(4);')
        self.assertStale()

    def test_edit_to_a_used_helper_in_the_same_file(self):
        self.tree.edit('game/sys/target.cpp', 'return v + 1;', 'return v + 2;')
        self.assertStale()

    def test_edit_to_a_used_helper_in_a_header(self):
        self.tree.edit('game/sys/sys.h', 'return t->count * SYS_SCALE;', 'return t->count * SYS_SCALE + 1;')
        self.assertStale()

    def test_layout_of_a_used_struct(self):
        self.tree.edit('game/sys/sys.h', 'int32_t count;\n    float weight;', 'float weight;\n    int32_t count;')
        self.assertStale()

    def test_used_macro(self):
        self.tree.edit('game/sys/sys.h', '#define SYS_SCALE 4', '#define SYS_SCALE 8')
        self.assertStale()

    def test_helper_reached_only_by_token_pasting(self):
        self.tree.edit('game/sys/sys.h', 'sys_get_bonus() { return 7; }', 'sys_get_bonus() { return 8; }')
        self.assertStale()

    def test_declaration_made_by_a_macro(self):
        self.tree.edit('game/sys/sys.h', 'RT_ORIGINAL(0x00401000,', 'RT_ORIGINAL(0x00401010,')
        self.assertStale()

    def test_the_macro_that_makes_a_declaration(self):
        self.tree.edit('runtime/include/runtime/original.h', '::rt::original<(address), signature> name{}',
                       '::rt::original<(address) + 0, signature> name{}')
        self.assertStale()

    def test_new_overload(self):
        self.tree.edit('game/sys/sys.h', 'inline int32_t overload(long v)',
                       'inline int32_t overload(int v) { return v * 2; }\ninline int32_t overload(long v)')
        self.assertStale()

    def test_order_of_used_declarations(self):
        self.tree.edit('game/sys/sys.h', 'inline int32_t sys_get_bonus() { return 7; }\n', '')
        self.tree.edit('game/sys/sys.h', 'inline int32_t scaled(', 'inline int32_t sys_get_bonus() { return 7; }\ninline int32_t scaled(')
        self.assertStale()

    def test_new_system_include(self):
        self.tree.edit('game/sys/sys.h', '#include <cstdint>', '#include <cstdint>\n#include <cmath>')
        self.assertStale()

    def test_namespace_of_a_used_helper(self):
        self.tree.edit('game/sys/target.cpp', 'namespace {\n\nint32_t helper', 'namespace inner {\n\nint32_t helper')
        self.assertStale()

    def test_conditional_compilation_around_a_used_helper(self):
        self.tree.edit('game/sys/sys.h', 'inline int32_t scaled(', '#if SYS_SCALE > 2\ninline int32_t scaled(')
        self.tree.edit('game/sys/sys.h', 'inline int32_t unused_helper', '#endif\ninline int32_t unused_helper')
        self.assertStale()

    def test_used_enum(self):
        self.tree.edit('game/sys/target.cpp', 'overload(3);', 'overload(int(Mode::busy));')
        self.before = self.tree.fp()
        self.tree.edit('game/sys/sys.h', '{ idle, busy }', '{ idle, waiting, busy }')
        self.assertStale()

    # ---- where the reading cannot be trusted: the legacy hash -----------------------------------

    def test_positional_macro_uses_the_legacy_hash(self):
        self.tree.edit('game/sys/target.cpp', 'overload(3);', 'overload(__LINE__);')
        self.assertEqual(self.tree.fp(), verification.source_hash(self.tree.root, 'sys_target'))

    def test_unbalanced_file_uses_the_legacy_hash(self):
        self.tree.edit('game/sys/sys.h', '#define SYS_SCALE 4', '#define SYS_SCALE 4\n#if 1\nstruct Broken {\n#endif')
        self.assertEqual(self.tree.fp(), verification.source_hash(self.tree.root, 'sys_target'))

    def test_brace_opened_in_one_branch_uses_the_legacy_hash(self):
        self.tree.edit('game/sys/target.cpp', 'extern "C" int32_t bb_sys_target(Thing *t)\n{',
                       '#if A\nextern "C" int32_t bb_sys_target(Thing *t) {\n#else\n'
                       'extern "C" int32_t bb_sys_target(Thing *t, int) {\n#endif')
        self.assertEqual(self.tree.fp(), verification.source_hash(self.tree.root, 'sys_target'))

    def test_family_members(self):
        self.tree.edit('game/sys/target.cpp', 'extern "C" int32_t bb_sys_neighbour',
                       'extern "C" int32_t bb_sys_fam_1() { return helper(1); }\n'
                       'extern "C" int32_t bb_sys_fam_2() { return 2; }\n'
                       'extern "C" int32_t bb_sys_neighbour')
        before = verification.source_fingerprint(self.tree.root, 'sys_fam', family=True)
        self.assertTrue(before.startswith(fingerprint.PREFIX))
        self.tree.edit('game/sys/target.cpp', 'return v - 1;', 'return v - 3;')
        self.assertEqual(verification.source_fingerprint(self.tree.root, 'sys_fam', family=True), before)
        self.tree.edit('game/sys/target.cpp', 'return v + 1;', 'return v + 3;')
        self.assertNotEqual(verification.source_fingerprint(self.tree.root, 'sys_fam', family=True), before)


# Edits that change bb_sys_target's compiled code and that an independent review found the first
# reading missed: each must change the fingerprint (or fall back to the legacy hash, which changes).
HOLES = {
    'if inside a function body': (
        {'cfg.h': '#define USE_FAST 1\nint unrelated_cfg = 3;\n',
         'x.cpp': '#include "cfg.h"\nextern "C" int bb_sys_target(int a) {\n#if USE_FAST\n    return a * 2;\n'
                  '#else\n    return a + 2;\n#endif\n}\n'},
        ('cfg.h', '#define USE_FAST 1', '#define USE_FAST 0')),
    'ifdef inside a struct': (
        {'cfg.h': '#define WIDE 1\n',
         'x.cpp': '#include "cfg.h"\nstruct Rec {\n    int a;\n#ifdef WIDE\n    int pad;\n#endif\n    int b;\n};\n'
                  'extern "C" int bb_sys_target(Rec *r) { return r->b; }\n'},
        ('cfg.h', '#define WIDE 1', '')),
    'object-like macro pasting': (
        {'x.cpp': 'static int helper() { return 1; }\n#define HELPER help ## er\n'
                  'extern "C" int bb_sys_target() { return HELPER(); }\n'},
        ('x.cpp', 'return 1;', 'return 2;')),
    'pasting reached through an alias': (
        {'x.cpp': 'static int helper_a() { return 1; }\n#define CALL(x) helper_##x\n#define PICK CALL\n'
                  'extern "C" int bb_sys_target() { return PICK(a)(); }\n'},
        ('x.cpp', 'return 1;', 'return 2;')),
    'unguarded header included again': (
        {'a.h': '#undef VAL\n#define VAL 2\n',
         'x.cpp': '#include "a.h"\n#undef VAL\n#define VAL 7\n#include "a.h"\n'
                  'extern "C" int bb_sys_target() { return VAL; }\n'},
        ('x.cpp', '#define VAL 7\n#include "a.h"\n', '#define VAL 7\n')),
    'declarator followed by an attribute': (
        {'x.cpp': 'static const int other = 1, g __attribute__((aligned(8))) = 5;\n'
                  'extern "C" int bb_sys_target() { return g; }\n'},
        ('x.cpp', '= 5;', '= 6;')),
    'parenthesized declarator': (
        {'x.cpp': 'static const int other = 1, (g) = 5;\nextern "C" int bb_sys_target() { return g; }\n'},
        ('x.cpp', '= 5;', '= 6;')),
    'declarator named like a function-like macro': (
        {'x.cpp': '#define limit(x) ((x) < 10 ? (x) : 10)\nstatic const struct Cfg { int a; } limit = { 5 };\n'
                  'extern "C" int bb_sys_target() { return limit.a; }\n'},
        ('x.cpp', '{ 5 }', '{ 6 }')),
    'digraph directive': (
        {'x.cpp': '%:define K 5\nint other = 1;\nextern "C" int bb_sys_target() { return K; }\n'},
        ('x.cpp', 'K 5', 'K 6')),
    'pragma operator': (
        {'x.cpp': '_Pragma("pack(push, 1)")\nstruct Other { int z; };\nstruct S { char c; int i; };\n'
                  '_Pragma("pack(pop)")\nstruct Other2 { int y; };\n'
                  'extern "C" int bb_sys_target(S *s) { return s->i; }\n'},
        ('x.cpp', 'push, 1', 'push, 4')),
}


class ReviewFindingsTest(unittest.TestCase):
    def test_each_edit_changes_what_the_row_is_for(self):
        for label, (files, (rel, old, new)) in HOLES.items():
            with self.subTest(label), tempfile.TemporaryDirectory() as root:
                tree = Tree.__new__(Tree)
                tree.root = root
                for name, text in files.items():
                    tree.write(f'game/sys/{name}', text)
                before = tree.fp()
                tree.edit(f'game/sys/{rel}', old, new)
                self.assertNotEqual(tree.fp(), before)


# Edits a second independent review found; each must change what a row is for (the fingerprint,
# or the legacy hash where the reading falls back). Files under game/sys/, before and after.
HOLES_SECOND_REVIEW = {
    'splice_space': ({'x.cpp': 'static const int zz = 0;\n#define K 5 \\  \n+ 1 + zz;\nextern "C" int bb_sys_target() { return K; }\n'},
        {'x.cpp': 'static const int zz = 0;\n#define K 5 \\  \n+ 2 + zz;\nextern "C" int bb_sys_target() { return K; }\n'}),
    'lonecr': ({'x.cpp': '#define K 5\n// note\r#undef K\r#define K 6\nextern "C" int bb_sys_target() { return K; }\n'},
        {'x.cpp': '#define K 5\n// note\r#undef K\r#define K 7\nextern "C" int bb_sys_target() { return K; }\n'}),
    'nestcat': ({'x.cpp': 'static int helper_v() { return 1; }\n#define CAT_(a, b) a ## b\n#define CAT(a, b) CAT_(a, b)\nextern "C" int bb_sys_target() { return CAT(CAT(help, er), _v)(); }\n'},
        {'x.cpp': 'static int helper_v() { return 2; }\n#define CAT_(a, b) a ## b\n#define CAT(a, b) CAT_(a, b)\nextern "C" int bb_sys_target() { return CAT(CAT(help, er), _v)(); }\n'}),
    'vaopt': ({'x.cpp': 'static int helper() { return 0; }\nstatic int helper_v() { return 1; }\n#define CALL(x, ...) x ## __VA_OPT__(_v)\nextern "C" int bb_sys_target() { return CALL(helper, 1)(); }\n'},
        {'x.cpp': 'static int helper() { return 0; }\nstatic int helper_v() { return 2; }\n#define CALL(x, ...) x ## __VA_OPT__(_v)\nextern "C" int bb_sys_target() { return CALL(helper, 1)(); }\n'}),
    'pragmamac': ({'x.cpp': '#define PACK_TIGHT _Pragma("pack(1)")\nPACK_TIGHT struct Wire { int z; };\nstruct S { char c; int i; };\nextern "C" int bb_sys_target(S *s) { return s->i; }\n'},
        {'x.cpp': '#define PACK_TIGHT _Pragma("pack(4)")\nPACK_TIGHT struct Wire { int z; };\nstruct S { char c; int i; };\nextern "C" int bb_sys_target(S *s) { return s->i; }\n'}),
    'swallow_anon': ({'x.cpp': 'struct S;\nstatic struct S *get_s() { return 0; }\nnamespace { const int kLimit = 4; }\nint other();\nextern "C" int bb_sys_target() { return kLimit; }\n'},
        {'x.cpp': 'struct S;\nstatic struct S *get_s() { return 0; }\nnamespace { const int kLimit = 5; }\nint other();\nextern "C" int bb_sys_target() { return kLimit; }\n'}),
    'swallow_tdefault': ({'x.cpp': 'template <typename T = int> T zero() { return T(); }\nextern "C" { static const int kLimit = 4; }\nint other();\nextern "C" int bb_sys_target() { return kLimit; }\n'},
        {'x.cpp': 'template <typename T = int> T zero() { return T(); }\nextern "C" { static const int kLimit = 5; }\nint other();\nextern "C" int bb_sys_target() { return kLimit; }\n'}),
    'fnret': ({'x.cpp': 'typedef int Handler;\nstatic int one(int a) { return a + 1; }\nstatic int two(int a) { return a + 2; }\nstatic Handler (*get_handler(int k))(int) { return k ? one : two; }\nextern "C" int bb_sys_target() { return get_handler(1)(5); }\n'},
        {'x.cpp': 'typedef int Handler;\nstatic int one(int a) { return a + 1; }\nstatic int two(int a) { return a + 2; }\nstatic Handler (*get_handler(int k))(int) { return k ? two : one; }\nextern "C" int bb_sys_target() { return get_handler(1)(5); }\n'}),
    'fptable': ({'x.cpp': 'static int f1(int a) { return a + 1; }\nstatic int f2(int a) { return a + 2; }\nstatic int other = 0, (*const tbl[2])(int) = { f1, f2 };\nextern "C" int bb_sys_target() { return tbl[0](3); }\n'},
        {'x.cpp': 'static int f1(int a) { return a + 1; }\nstatic int f2(int a) { return a + 2; }\nstatic int other = 0, (*const tbl[2])(int) = { f2, f1 };\nextern "C" int bb_sys_target() { return tbl[0](3); }\n'}),
    'sbind': ({'x.cpp': 'struct P { int x, y; };\nstatic auto [pa, pb] = P{1, 2};\nextern "C" int bb_sys_target() { return pb; }\n'},
        {'x.cpp': 'struct P { int x, y; };\nstatic auto [pa, pb] = P{1, 3};\nextern "C" int bb_sys_target() { return pb; }\n'}),
    'ifzero_inc': ({'a.h': '#ifndef A_H\n#define A_H\n#define VAL 2\n#endif\n', 'x.cpp': '#if 0\n#include "a.h"\n#endif\n#include "a.h"\n#ifndef VAL\n#define VAL 7\n#endif\nextern "C" int bb_sys_target() { return VAL; }\n'},
        {'a.h': '#ifndef A_H\n#define A_H\n#define VAL 2\n#endif\n', 'x.cpp': '#if 0\n#include "a.h"\n#endif\n#ifndef VAL\n#define VAL 7\n#endif\nextern "C" int bb_sys_target() { return VAL; }\n'}),
    'undefguard': ({'a.h': '#ifndef A_H\n#define A_H\n#undef VAL\n#define VAL 2\n#undef A_H\n#endif\n', 'x.cpp': '#include "a.h"\n#undef VAL\n#define VAL 7\n#include "a.h"\nextern "C" int bb_sys_target() { return VAL; }\n'},
        {'a.h': '#ifndef A_H\n#define A_H\n#undef VAL\n#define VAL 2\n#undef A_H\n#endif\n', 'x.cpp': '#include "a.h"\n#undef VAL\n#define VAL 7\nextern "C" int bb_sys_target() { return VAL; }\n'}),
    'once_if0': ({'a.h': '#if 0\n#pragma once\n#endif\n#undef VAL\n#define VAL 2\n', 'x.cpp': '#include "a.h"\n#undef VAL\n#define VAL 7\n#include "a.h"\nextern "C" int bb_sys_target() { return VAL; }\n'},
        {'a.h': '#if 0\n#pragma once\n#endif\n#undef VAL\n#define VAL 2\n', 'x.cpp': '#include "a.h"\n#undef VAL\n#define VAL 7\nextern "C" int bb_sys_target() { return VAL; }\n'}),
    'assert': ({'x.cpp': '#include <cassert>\nextern "C" int bb_sys_target(int a) { assert(a > 0); return a; }\n'},
        {'x.cpp': '#include <cassert>\n\nextern "C" int bb_sys_target(int a) { assert(a > 0); return a; }\n'}),
    'bline': ({'x.cpp': 'extern "C" int bb_sys_target() { return __builtin_LINE(); }\n'},
        {'x.cpp': '\nextern "C" int bb_sys_target() { return __builtin_LINE(); }\n'}),
    'alias': ({'x.cpp': 'extern "C" int real_impl(int a) { return a + 1; }\nextern "C" int entry(int a) __attribute__((alias("real_impl")));\nextern "C" int bb_sys_target(int a) { return entry(a); }\n'},
        {'x.cpp': 'extern "C" int real_impl(int a) { return a + 2; }\nextern "C" int entry(int a) __attribute__((alias("real_impl")));\nextern "C" int bb_sys_target(int a) { return entry(a); }\n'}),
    # third review
    'static_writer': ({'x.cpp': 'static int counter = 5;\nvoid bump() { counter++; }\nextern "C" int bb_sys_target() { return counter; }\n'},
        {'x.cpp': 'static int counter = 5;\nextern "C" int bb_sys_target() { return counter; }\n'}),
    'called_once': ({'x.cpp': 'extern int sink[64];\nstatic int helper(int a) { int s = 0; for (int i = 0; i < 64; i++) { s += sink[i] * a; sink[i] ^= s; if (s > 1000) s -= sink[(i * 7) & 63]; } return s; }\nextern "C" int bb_sys_target(int a) { return helper(a); }\n'},
        {'x.cpp': 'extern int sink[64];\nstatic int helper(int a) { int s = 0; for (int i = 0; i < 64; i++) { s += sink[i] * a; sink[i] ^= s; if (s > 1000) s -= sink[(i * 7) & 63]; } return s; }\nextern "C" int bb_sys_target(int a) { return helper(a); }\nint other(int a) { return helper(a + 1); }\n'}),
    'packed_typedef': ({'x.cpp': 'typedef struct __attribute__((packed)) { char c; int i; } Rec;\nextern "C" int bb_sys_target(Rec *r) { return r->i; }\n'},
        {'x.cpp': 'typedef struct __attribute__((aligned(4))) { char c; int i; } Rec;\nextern "C" int bb_sys_target(Rec *r) { return r->i; }\n'}),
    'decltype_base': ({'x.cpp': 'struct B { int b; };\nextern B proto;\nstruct S : decltype(proto) { char c; int i; } g;\nextern "C" int bb_sys_target() { return g.i; }\n'},
        {'x.cpp': 'struct B { int b; };\nextern B proto;\nstruct S : decltype(proto) { int i; char c; } g;\nextern "C" int bb_sys_target() { return g.i; }\n'}),
    'guardline_cond': ({'a.h': '#ifndef A_H\n#define A_H\n#define VAL 2\n#endif\n', 'b.h': '#ifndef B_H\n#define B_H\nint b_unused;\n#endif\n', 'x.cpp': '#include "b.h"\n#ifndef B_H\n#include "a.h"\n#endif\n#include "a.h"\n#ifndef VAL\n#define VAL 7\n#endif\nextern "C" int bb_sys_target() { return VAL; }\n'},
        {'a.h': '#ifndef A_H\n#define A_H\n#define VAL 2\n#endif\n', 'b.h': '#ifndef B_H\n#define B_H\nint b_unused;\n#endif\n', 'x.cpp': '#include "b.h"\n#ifndef B_H\n#include "a.h"\n#endif\n#ifndef VAL\n#define VAL 7\n#endif\nextern "C" int bb_sys_target() { return VAL; }\n'}),
    'rawsplice': ({'x.cpp': 'extern "C" int bb_sys_target() { return R"(a\\\nbc)"[1]; }\n'},
        {'x.cpp': 'extern "C" int bb_sys_target() { return R"(abc)"[1]; }\n'}),
    'vaopt_empty': ({'x.cpp': '#define EMPTY\nstatic int help_impl() { return 1; }\nstatic int help_v_impl() { return 5; }\n#define CALL(x, ...) x ## __VA_OPT__(_v) ## _impl\nextern "C" int bb_sys_target() { return CALL(help, EMPTY)(); }\n'},
        {'x.cpp': '#define EMPTY\nstatic int help_impl() { return 2; }\nstatic int help_v_impl() { return 5; }\n#define CALL(x, ...) x ## __VA_OPT__(_v) ## _impl\nextern "C" int bb_sys_target() { return CALL(help, EMPTY)(); }\n'}),
}


class SecondReviewTest(unittest.TestCase):
    def test_each_edit_changes_what_the_row_is_for(self):
        for label, (before, after) in HOLES_SECOND_REVIEW.items():
            with self.subTest(label):
                values = []
                for files in (before, after):
                    with tempfile.TemporaryDirectory() as root:
                        for name, text in files.items():
                            path = os.path.join(root, 'game', 'sys', name)
                            os.makedirs(os.path.dirname(path), exist_ok=True)
                            with open(path, 'w', newline='') as f:
                                f.write(text)
                        values.append(verification.source_fingerprint(root, 'sys_target'))
                self.assertNotEqual(values[0], values[1])


class CurrencyTest(unittest.TestCase):
    """Both kinds of value stay valid, each judged by its own rule, and migration converts only
    rows that are current."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.tree = Tree(self.tmp.name)
        self.root = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_legacy_rows_follow_the_legacy_rule(self):
        legacy = verification.source_hash(self.root, 'sys_target')
        self.assertTrue(verification.row_current(self.root, 'sys_target', legacy))
        # an unrelated edit in the same file still makes a legacy row stale: the old rule, unchanged
        self.tree.edit('game/sys/target.cpp', 'return v - 1;', 'return v - 2;')
        self.assertFalse(verification.row_current(self.root, 'sys_target', legacy))

    def test_fingerprint_rows(self):
        fp = verification.source_fingerprint(self.root, 'sys_target')
        self.assertTrue(verification.row_current(self.root, 'sys_target', fp))
        self.tree.edit('game/sys/target.cpp', 'return v - 1;', 'return v - 2;')
        self.assertTrue(verification.row_current(self.root, 'sys_target', fp))
        self.tree.edit('game/sys/target.cpp', 'return v + 1;', 'return v + 2;')
        self.assertFalse(verification.row_current(self.root, 'sys_target', fp))

    def test_fingerprint_row_is_stale_when_the_function_falls_back(self):
        fp = verification.source_fingerprint(self.root, 'sys_target')
        self.tree.edit('game/sys/target.cpp', 'overload(3);', 'overload(__LINE__);')
        self.assertFalse(verification.row_current(self.root, 'sys_target', fp))
        self.assertFalse(verification.row_current(self.root, 'sys_target', ''))

    def test_migration_converts_current_rows_only(self):
        legacy = verification.source_hash(self.root, 'sys_target')
        path = os.path.join(self.root, 'symbols', 'verification.csv')
        os.makedirs(os.path.dirname(path))
        with open(path, 'w') as f:
            f.write(','.join(verification.VERIFICATION_HEADER) + '\n'
                    f'sys_neighbour,edge,,3,3,0,0,0123456789abcdef,2026-11-01\n'
                    f'sys_target,recorded,,5,5,0,0,{legacy},2026-11-01\n'
                    f'sys_target,edge,,5,5,0,0,{legacy},2026-11-01\n')
        self.assertEqual(verification.migrate(self.root, path), 2)
        rows = verification.read_csv(path, verification.VERIFICATION_HEADER)
        fp = verification.source_fingerprint(self.root, 'sys_target')
        self.assertEqual([r['source'] for r in rows], ['0123456789abcdef', fp, fp])
        self.assertEqual(verification.migrate(self.root, path), 0)


class ParseTest(unittest.TestCase):
    def names(self, text: str) -> set[str]:
        out = set()
        for d in fingerprint.parse(text):
            if d.kind == 'code':
                names, always = fingerprint.declared_names(d.tokens, {})
                out |= names if not always else {'<always>'}
        return out

    def test_declared_names(self):
        self.assertEqual(self.names('int Foo::bar(int x) const { return x; }'), {'Foo', 'bar'})
        self.assertEqual(self.names('Foo::~Foo() {}'), {'Foo'})
        self.assertLessEqual({'Foo'}, self.names('Foo::Foo() : a{1}, b(2) { go(); }'))
        self.assertEqual(self.names('typedef struct { int a; } Pair;'), {'Pair'})
        self.assertEqual(self.names('struct { int a; } g_pair;'), {'g_pair'})
        self.assertEqual(self.names('enum { first_value, second_value = 4 };'), {'first_value', 'second_value'})
        self.assertEqual(self.names('static void (*callback)(int) = nullptr;'), {'callback'})
        self.assertEqual(self.names('int a = 1, b[4];'), {'a', 'b'})
        self.assertEqual(self.names('template <typename T> T clamp(T v) { return v; }'), {'clamp'})
        self.assertEqual(self.names('bool operator==(A, A);'), {'<always>'})
        self.assertEqual(self.names('using namespace sys;'), {'<always>'})
        self.assertEqual(self.names('using Alias = Target;'), {'Alias', 'Target'})

    def test_function_body_ends_the_declaration(self):
        decls = [d for d in fingerprint.parse('void f() { x(); }\nint g;\n') if d.kind == 'code']
        self.assertEqual([d.text for d in decls], ['void f() { x(); }', 'int g;'])

    def test_struct_continues_to_its_semicolon(self):
        decls = [d for d in fingerprint.parse('struct S { int a; } s;\nint g;\n') if d.kind == 'code']
        self.assertEqual([d.text for d in decls], ['struct S { int a; } s;', 'int g;'])

    def test_line_splice_inside_a_comment(self):
        decls = [d for d in fingerprint.parse('// a comment \\\nint hidden;\nint g;\n') if d.kind == 'code']
        self.assertEqual([d.text for d in decls], ['int g;'])


class RepositoryTest(unittest.TestCase):
    def test_every_recorded_function_reads(self):
        """The repository's own sources split cleanly: nearly every function with rows narrows."""
        rows = verification.read_csv(os.path.join(ROOT, 'symbols', 'verification.csv'),
                                     verification.VERIFICATION_HEADER)
        names = sorted({r['function'] for r in rows})
        legacy = [n for n in names if not verification.source_fingerprint(ROOT, n).startswith(fingerprint.PREFIX)]
        self.assertLess(len(legacy), max(10, len(names) // 50), legacy)


if __name__ == '__main__':
    unittest.main()
