#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A replacement's source fingerprint: its own code and every declaration that code can reach.

usage: fingerprint.py NAME [--family] [--root DIR] [--explain]   print a function's fingerprint
       fingerprint.py --stats [--root DIR]                         how many functions narrow
       fingerprint.py --migrate [--root DIR]                       convert current legacy rows

A row of symbols/verification.csv says which code its counts were measured on. The legacy hash
(verification.source_hash) covers the replacement's defining files and every header they include
from game/ and runtime/include/, whole: one edit to a shared header leaves the rows of every
function that includes it stale, even functions that never use what changed. The fingerprint
covers the same files, but only the parts of them the function can reach:

1. Each file is split into its top-level declarations (a function with its body, a struct with its
   members, a variable with its initializer, a typedef, a macro definition), after the comments
   are removed and the whitespace between tokens is reduced to "some" or "none".
2. The translation unit is put back together in include order, so each declaration has its place.
3. Starting from the replacement's definition (bb_NAME), the closure takes, for every identifier in
   a declaration it holds (directive lines inside it included, and in its macro expansion,
   rescanned with what follows), every declaration of that
   name in the translation unit and every macro definition of it, until nothing new is reached.
   Matching is by name only: every overload, every forward declaration, every member defined out of
   line under its class's name. A reached declaration with internal linkage (anonymous namespace,
   namespace-scope static) also brings in every declaration that names it: the compiler sees all
   its uses. Some declarations are always in the closure: free operator
   overloads, using-directives and using-declarations, #pragma lines and declarations holding a
   _Pragma or #pragma, includes whose content is not in the repository (system headers), a second
   include of a header without a guard, and anything whose declared names could not be read.
4. The fingerprint hashes the closure in translation-unit order: each declaration's file, the
   conditional-compilation lines around it, the namespaces it is in, and its text.

An edit to a declaration the function does not reach by name, or to a comment, does not change the
fingerprint. An edit to the function, to anything it names (a helper, a struct and so to its
layout, a constant, a macro), to anything those name in turn, the order of any of them, a new
overload, or an added system include does.

Where the reading cannot be trusted, the fingerprint is the legacy hash itself (the old rule, no
narrower): a file that does not split cleanly (unbalanced braces, an unterminated comment or
string, a conditional that opens a brace in one branch only, a digraph), a replacement whose
definition is
not found, an #include inside a declaration, a token-pasting macro that cannot be expanded, or a
closure that reaches __LINE__, __COUNTER__, __DATE__, __TIME__ or __TIMESTAMP__ (their values
depend on where the code sits, not on what it says). docs/VERIFY.md, "Source fingerprints", has
the argument for why this is never weaker than the legacy rule for a change of behaviour.

Fingerprints are written `f1:` and 16 hex digits; legacy hashes are 16 hex digits. Both stay valid
in symbols/verification.csv: a row is current when its value equals what its own rule computes
for the code now.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import os
import re
import sys

PREFIX = 'f1:'
VERSION = b'paleblood-fingerprint-1\0'
HASH_LENGTH = 16

# Macros whose value depends on where code sits or when it was built. A closure that reaches one of
# them uses the legacy hash.
POSITIONAL = frozenset({'__LINE__', '__COUNTER__', '__DATE__', '__TIME__', '__TIMESTAMP__',
                        '__INCLUDE_LEVEL__', '__BASE_FILE__', '__builtin_LINE', '__builtin_COLUMN',
                        '__builtin_FILE', '__builtin_FILE_NAME', '__builtin_FUNCTION',
                        '__builtin_source_location', 'source_location',
                        # assert (<cassert>, <assert.h>) expands to __LINE__ inside a system header
                        'assert', '_assert', '__assert', '__assert_fail', '__ASSERT_FUNCTION'})
MAX_EXPANSION_TOKENS = 200_000
MAX_EXPANSION_DEPTH = 32

KEYWORDS = frozenset('''
alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t
char32_t class compl concept const consteval constexpr constinit const_cast continue co_await
co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern
false float for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr
operator or or_eq private protected public register reinterpret_cast requires return short signed
sizeof static static_assert static_cast struct switch template this thread_local throw true try
typedef typeid typename union unsigned using virtual void volatile wchar_t while xor xor_eq final
override _Alignas _Alignof _Atomic _Bool _Generic _Noreturn _Static_assert _Thread_local restrict
__attribute__ __declspec __asm__ __asm __restrict __restrict__ __inline __inline__ __volatile__
__extension__ __typeof__ __alignof__ __builtin_offsetof offsetof defined
'''.split())
TAG_KEYWORDS = frozenset({'struct', 'class', 'union', 'enum'})
DECLARATOR_FOLLOWERS = frozenset({'(', '=', ';', ',', '[', '{', ':'})

TOKEN = re.compile(r'''
    (?P<ws>[ \t\r\f\v]+)
  | (?P<nl>\n)
  | (?P<lc>//[^\n]*)
  | (?P<bc>/\*.*?\*/)
  | (?P<ubc>/\*)
  | (?P<raw>(?:u8|u|U|L)?R"(?P<delim>[^()\\\s]{0,16})\()
  | (?P<str>(?:u8|u|U|L)?"(?:[^"\\\n]|\\.)*")
  | (?P<chr>(?:u8|u|U|L)?'(?:[^'\\\n]|\\.)+')
  | (?P<ustr>(?:u8|u|U|L)?["'])
  | (?P<num>\.?\d(?:'?[\w]|[eEpP][+-]|\.)*)
  | (?P<id>[A-Za-z_]\w*)
  | (?P<digraph><%|%>|%:|:>|<:(?!:(?![:>])))
  | (?P<punct>::|->|\#\#|\.\.\.|[^\s\w])
''', re.S | re.X)


class ParseError(Exception):
    pass


class Tok:
    __slots__ = ('kind', 'text', 'space')

    def __init__(self, kind: str, text: str, space: bool):
        self.kind, self.text, self.space = kind, text, space

    def __repr__(self) -> str:
        return f'Tok({self.kind},{self.text!r})'


def render(tokens: list[Tok]) -> str:
    """The tokens with one space wherever the source had any white space or comment between them."""
    out = []
    for i, t in enumerate(tokens):
        if i and t.space:
            out.append(' ')
        out.append(t.text)
    return ''.join(out)


def lex(text: str) -> list[tuple[bool, list[Tok]]]:
    """Logical lines (line splices joined) as (is_directive, tokens), comments removed. Code
    between directives is one run: only directive lines are line-bound."""
    # Line ends: CR LF and a lone CR end a line; a backslash before a line end (blanks between
    # allowed, as GCC and Clang accept) joins the lines.
    if re.search(r'R"', text) and re.search(r'\\[ \t\f\v]*(?:\r\n|\n|\r)|\r(?!\n)', text):
        raise ParseError('a raw string literal with line splices or carriage returns about')
    text = re.sub(r'\\[ \t\f\v]*\n', '', text.replace('\r\n', '\n').replace('\r', '\n'))
    out: list[tuple[bool, list[Tok]]] = []
    cur: list[Tok] = []
    in_directive = False
    line_start = True
    space = False
    pos, n = 0, len(text)
    while pos < n:
        m = TOKEN.match(text, pos)
        if not m:
            raise ParseError(f'cannot read character {text[pos]!r}')
        kind = m.lastgroup
        pos = m.end()
        if kind in ('ws', 'lc', 'bc'):
            space = True
            continue
        if kind == 'ubc':
            raise ParseError('unterminated comment')
        if kind == 'ustr':
            raise ParseError('unterminated string or character literal')
        if kind == 'digraph':
            raise ParseError(f'digraph {m.group()!r}')
        if kind == 'nl':
            if in_directive:
                out.append((True, cur))
                cur, in_directive = [], False
            line_start = space = True
            continue
        if kind == 'raw':
            end = text.find(')' + m.group('delim') + '"', pos)
            if end < 0:
                raise ParseError('unterminated raw string')
            pos = end + len(m.group('delim')) + 2
            if in_directive and '\n' in text[m.start():pos]:
                raise ParseError('raw string across lines in a directive')
            tok = Tok('str', text[m.start():pos], space)
        elif kind in ('str', 'chr'):
            tok = Tok('str', m.group(), space)
        else:
            tok = Tok(kind, m.group(), space)
        if tok.text == '#' and line_start and not in_directive:
            if cur:
                out.append((False, cur))
            cur, in_directive = [tok], True
        else:
            cur.append(tok)
        line_start = space = False
    if cur:
        out.append((in_directive, cur))
    return out


# ---- one file -------------------------------------------------------------------------------

class Decl:
    """A top-level declaration, macro definition or directive, as it appears in one file."""
    __slots__ = ('kind', 'tokens', 'text', 'conds', 'scope', 'name', 'include', 'always', 'position', 'file')

    def __init__(self, kind, tokens, conds, scope, name='', include=None, always=False):
        self.kind = kind                # 'code', 'macro', 'include', 'pragma', 'other'
        self.tokens = tokens
        self.text = render(tokens)
        self.conds = conds              # the enclosing #if/#elif/#else lines, outermost first
        self.scope = scope              # the enclosing namespace / extern "C" headers
        self.name = name                # a macro's name
        self.include = include          # an include's (form, path)
        self.always = always
        self.position = 0
        self.file = ''


def parse(text: str) -> list[Decl]:
    """The file's top-level items in order. Raises ParseError when it does not split cleanly."""
    items: list[Decl] = []
    conds: list[list[str]] = []
    scopes: list[str] = []
    cur: list[Tok] = []
    depth = {'(': 0, '[': 0, '{': 0}
    head_end = None          # index in cur of the first top-level '{'
    pending_end = False      # a function body closed: the declaration ends unless a punctuator follows

    def cond_snapshot() -> tuple:
        return tuple(' '.join(c) for c in conds)

    def finish() -> None:
        nonlocal cur, head_end, pending_end
        if cur:
            items.append(Decl('code', cur, cur_conds, tuple(scopes)))
        cur, head_end, pending_end = [], None, False

    cur_conds: tuple = ()
    for is_dir, toks in lex(text):
        if is_dir:
            if pending_end:
                finish()
            word = toks[1].text if len(toks) > 1 and toks[1].kind == 'id' else ''
            line = render(toks)
            inside = bool(cur)
            if word in ('if', 'ifdef', 'ifndef'):
                conds.append([line])
            elif word in ('elif', 'else', 'elifdef', 'elifndef'):
                if not conds:
                    raise ParseError(f'#{word} without #if')
                conds[-1].append(line)
            elif word == 'endif':
                if not conds:
                    raise ParseError('#endif without #if')
                conds.pop()
            if inside:
                # A directive inside a declaration is part of its text.
                cur.append(Tok('pp', '\n' + line + '\n', True))
                if word == 'include':
                    raise ParseError('#include inside a declaration')
                if word in ('define', 'undef'):
                    items.append(_macro(toks, cond_snapshot(), tuple(scopes)))
                continue
            snap = cond_snapshot()
            if word in ('if', 'ifdef', 'ifndef', 'elif', 'else', 'elifdef', 'elifndef', 'endif'):
                continue
            if word in ('define', 'undef'):
                items.append(_macro(toks, snap, tuple(scopes)))
            elif word in ('include', 'include_next', 'import'):
                items.append(Decl('include', toks, snap, tuple(scopes), include=_include_target(toks)))
            elif word == 'pragma':
                items.append(Decl('pragma', toks, snap, tuple(scopes), always=True))
            else:
                items.append(Decl('other', toks, snap, tuple(scopes), always=True))
            continue
        for t in toks:
            if pending_end:
                if (t.kind == 'punct' and t.text not in ('}', '~', '#', '[', ':', '::')) or t.text == 'catch':
                    pending_end = False
                else:
                    finish()
            if cur and head_end is not None and depth['{'] == 0 and (
                    t.text == 'namespace' or (t.kind == 'str' and cur[-1].text == 'extern')):
                raise ParseError('a namespace or extern block inside a declaration')
            if not cur:
                cur_conds = cond_snapshot()
            if t.kind == 'punct' and t.text in ('(', '['):
                depth[t.text] += 1
            elif t.kind == 'punct' and t.text in (')', ']'):
                key = '(' if t.text == ')' else '['
                depth[key] -= 1
                if depth[key] < 0:
                    raise ParseError(f'unbalanced {t.text}')
            elif t.kind == 'punct' and t.text == '{':
                if depth['{'] == 0 and depth['('] == 0 and depth['['] == 0:
                    if _opens_scope(cur):
                        scopes.append(render(cur))
                        cur, head_end = [], None
                        continue
                    if head_end is None:
                        head_end = len(cur)
                depth['{'] += 1
            elif t.kind == 'punct' and t.text == '}':
                if depth['{'] == 0:
                    if cur or not scopes:
                        raise ParseError('unbalanced }')
                    scopes.pop()
                    continue
                depth['{'] -= 1
                if depth['{'] == 0 and depth['('] == 0 and depth['['] == 0:
                    cur.append(t)
                    if _function_like(cur[:head_end]):
                        pending_end = True
                    continue
            elif t.kind == 'punct' and t.text == ';' and not any(depth.values()):
                cur.append(t)
                finish()
                continue
            cur.append(t)
    if pending_end:
        finish()
    if cur or any(depth.values()):
        raise ParseError('the file ends inside a declaration')
    if scopes:
        raise ParseError('the file ends inside a namespace')
    if conds:
        raise ParseError('the file ends inside #if')
    return items


def _opens_scope(cur: list[Tok]) -> bool:
    words = [t.text for t in cur]
    if words and words[0] == 'inline':
        words = words[1:]
    if words[:1] == ['namespace'] and all(w == '::' or re.fullmatch(r'\w+', w) for w in words[1:]):
        return True
    return len(cur) == 2 and words[0] == 'extern' and cur[1].kind == 'str'


FUNCTION_TAIL = frozenset({'const', 'volatile', '&', '&&', 'noexcept', 'override', 'final', 'throw',
                           'mutable', 'constexpr', 'requires', 'try'})


def _function_like(head: list[Tok]) -> bool:
    """Whether a declaration whose first top-level braces just closed ends there (a function body)
    rather than at the next ';' (a struct, enum, typedef or initializer): a head whose last
    top-level parenthesis group is followed only by qualifiers, attributes, a trailing return type
    or a constructor's initializer list."""
    head = head[_skip_prefix(head):]
    depth = 0
    last_close = None
    for k, t in enumerate(head):
        if t.text in ('(', '['):
            depth += 1
        elif t.text in (')', ']'):
            depth -= 1
            if depth == 0 and t.text == ')':
                last_close = k
        elif depth == 0 and t.text == '=' and last_close is None:
            return False                            # an initializer: int x = { ... };
    if last_close is None:
        return False
    # the group must belong to a declarator: not __attribute__((...)), alignas(...), decltype(...)
    depth, open_at = 0, None
    for k in range(last_close, -1, -1):
        depth += {')': 1, '(': -1}.get(head[k].text, 0)
        if depth == 0:
            open_at = k
            break
    before = head[open_at - 1] if open_at else None
    if before is None or before.text in ATTRIBUTE_WORDS or before.text in (
            'decltype', 'sizeof', 'alignof', 'noexcept', 'throw', 'requires', 'typeof', '__typeof__'):
        return False
    if before.kind != 'id' and before.text not in (')', '>'):
        return False
    tail = head[last_close + 1:]
    k = 0
    while k < len(tail):
        k = _skip_attributes(tail, k)
        if k >= len(tail):
            break
        x = tail[k].text
        if x in (':', '->'):
            return True                             # initializer list, trailing return type
        if x in FUNCTION_TAIL:
            if k + 1 < len(tail) and tail[k + 1].text == '(':
                try:
                    _, k = _arguments(tail, k + 1)
                except ExpansionError:
                    return False
                continue
            k += 1
            continue
        return False
    return True


def _macro(toks: list[Tok], conds: tuple, scope: tuple) -> Decl:
    if len(toks) < 3 or toks[2].kind != 'id':
        raise ParseError(f'malformed {render(toks)}')
    return Decl('macro', toks, conds, scope, name=toks[2].text)


def _include_target(toks: list[Tok]) -> tuple[str, str]:
    rest = toks[2:]
    if rest and rest[0].kind == 'str' and rest[0].text.startswith('"'):
        return ('quote', rest[0].text[1:-1])
    if rest and rest[0].text == '<':
        return ('angle', ''.join(t.text for t in rest[1:-1]))
    return ('other', render(rest))


# ---- macros ---------------------------------------------------------------------------------

class Macro:
    __slots__ = ('name', 'params', 'variadic', 'body', 'pastes')

    def __init__(self, decl: Decl):
        toks = decl.tokens
        self.name = toks[2].text
        self.params = None
        self.variadic = False
        rest = toks[3:]
        if toks[1].text == 'undef':
            self.body = []
        elif rest and rest[0].text == '(' and not rest[0].space:
            close = next((i for i, t in enumerate(rest) if t.text == ')'), None)
            if close is None:
                raise ParseError(f'malformed macro {self.name}')
            names = [t.text for t in rest[1:close] if t.text != ',']
            if names and names[-1] == '...':
                self.variadic = True
                if close >= 3 and rest[close - 2].kind == 'id':
                    names.pop()                     # GNU named variadic: args...
                else:
                    names[-1] = '__VA_ARGS__'
            self.params = names
            self.body = rest[close + 1:]
        else:
            self.body = rest
        self.pastes = any(t.text == '##' for t in self.body)


class ExpansionError(Exception):
    pass


def expand(tokens: list[Tok], macros: dict[str, list[Macro]], hide: frozenset = frozenset(),
           depth: int = 0, budget: list | None = None) -> list[Tok]:
    """Expands every macro the translation unit defines, with every definition it has (their
    expansions follow each other: what matters here is which names appear, not valid code). An
    expansion is rescanned together with the tokens after it, as the preprocessor does, so an
    object-like macro that expands to a function-like macro's name takes the arguments that follow."""
    budget = budget if budget is not None else [0]
    if depth > MAX_EXPANSION_DEPTH:
        raise ExpansionError('macro expansion too deep')
    pending = collections.deque((t, hide) for t in tokens)
    out: list[Tok] = []
    while pending:
        t, h = pending.popleft()
        defs = macros.get(t.text) if t.kind == 'id' and t.text not in h else None
        if not defs:
            out.append(t)
            continue
        args = None
        invocation: list[Tok] = []
        if any(m.params is not None for m in defs) and pending and pending[0][0].text == '(':
            args, invocation = _take_arguments(pending)
        produced: list[Tok] = []
        expanded_any = False
        for m in defs:
            if m.params is None:
                produced.extend(_join(m.body))
                produced.extend(invocation)       # an object-like alternative leaves the parentheses
            elif args is not None:
                produced.extend(_substitute(m, args, macros, h, depth, budget))
            else:
                continue
            expanded_any = True
        if not expanded_any:
            out.append(t)
            pending.extendleft((x, h) for x in reversed(invocation))
            continue
        budget[0] += len(produced) + 1
        if budget[0] > MAX_EXPANSION_TOKENS:
            raise ExpansionError('macro expansion too large')
        pending.extendleft((x, h | {t.text}) for x in reversed(produced))
    return out


def _take_arguments(pending) -> tuple[list[list[Tok]], list[Tok]]:
    """Pops a parenthesized argument list off the front of `pending`: (arguments, its tokens)."""
    taken: list[Tok] = []
    while pending:
        taken.append(pending.popleft()[0])
        if taken[0].text != '(':
            break
        try:
            args, end = _arguments(taken, 0)
        except ExpansionError:
            continue
        return args, taken
    raise ExpansionError('unterminated macro invocation')


def _arguments(tokens: list[Tok], at: int) -> tuple[list[list[Tok]], int]:
    """The arguments of an invocation whose '(' is tokens[at], and the index after its ')'."""
    args: list[list[Tok]] = [[]]
    depth = 0
    for j in range(at, len(tokens)):
        t = tokens[j]
        if t.text == '(':
            depth += 1
            if depth == 1:
                continue
        elif t.text == ')':
            depth -= 1
            if depth == 0:
                return args, j + 1
        elif t.text == ',' and depth == 1:
            args.append([])
            continue
        args[-1].append(t)
    raise ExpansionError('unterminated macro invocation')


def _substitute(m: Macro, args: list[list[Tok]], macros, hide, depth, budget) -> list[Tok]:
    params = m.params or []
    if params == [] and args == [[]]:
        args = []
    if m.variadic:
        fixed = len(params) - 1
        if len(args) < fixed:
            raise ExpansionError(f'{m.name}: too few arguments')
        va = []
        for k, a in enumerate(args[fixed:]):
            if k:
                va.append(Tok('punct', ',', False))
            va.extend(a)
        args = args[:fixed] + [va]
    elif len(args) != len(params):
        raise ExpansionError(f'{m.name}: {len(args)} arguments for {len(params)} parameters')
    values = dict(zip(params, args))
    present = bool(m.variadic and args and expand(args[-1], macros, hide, depth + 1, budget))
    body = _va_opt(m.body, present)
    out: list[Tok] = []
    i = 0
    while i < len(body):
        t = body[i]
        if t.text == '#' and i + 1 < len(body) and body[i + 1].text in values:
            out.append(Tok('str', '"' + render(values[body[i + 1].text]).replace('"', '\\"') + '"', t.space))
            i += 2
            continue
        if t.text in values:
            pasted = (i + 1 < len(body) and body[i + 1].text == '##') or (out and out[-1].text == '##')
            value = values[t.text] if pasted else expand(values[t.text], macros, hide, depth + 1, budget)
            out.extend(value)
            i += 1
            continue
        out.append(t)
        i += 1
    return _join(out)


def _va_opt(body: list[Tok], present: bool) -> list[Tok]:
    """__VA_OPT__(x) is x when variadic arguments are given, nothing otherwise."""
    out: list[Tok] = []
    i = 0
    while i < len(body):
        if body[i].text == '__VA_OPT__' and i + 1 < len(body) and body[i + 1].text == '(':
            args, end = _arguments(body, i + 1)
            if present:
                for k, a in enumerate(args):
                    if k:
                        out.append(Tok('punct', ',', False))
                    out.extend(a)
            i = end
            continue
        out.append(body[i])
        i += 1
    # an empty __VA_OPT__ between two ## leaves a placemarker: x ## ## y joins x and y
    collapsed: list[Tok] = []
    for t in out:
        if t.text == '##' and collapsed and collapsed[-1].text == '##':
            continue
        collapsed.append(t)
    return collapsed


def _join(out: list[Tok]) -> list[Tok]:
    """## joins its neighbours into one token."""
    joined: list[Tok] = []
    i = 0
    while i < len(out):
        if out[i].text == '##' and joined and i + 1 < len(out):
            left = joined.pop()
            text = left.text + out[i + 1].text
            kind = 'id' if re.fullmatch(r'[A-Za-z_]\w*', text) else ('num' if text[:1].isdigit() else 'punct')
            joined.append(Tok(kind, text, left.space))
            i += 2
            continue
        if out[i].text == '##':
            i += 1
            continue
        joined.append(out[i])
        i += 1
    return joined


IDENTIFIER = re.compile(r'[A-Za-z_]\w*')


def identifiers(tokens: list[Tok]) -> set[str]:
    """The identifiers of the tokens, those of directive lines inside a declaration included (an
    #if inside a function body or struct names the macros it depends on)."""
    out = {t.text for t in tokens if t.kind == 'id'}
    for t in tokens:
        if t.kind == 'pp':
            out.update(IDENTIFIER.findall(_without_strings(t.text)))
    return out


SYMBOL_STRING_WORDS = frozenset({'asm', '__asm__', '__asm', 'alias', 'ifunc', 'weakref', 'symver'})


def _symbol_strings(tokens: list[Tok]) -> set[str]:
    """Identifiers inside the string literals of asm(...) and alias/ifunc/weakref/symver attributes:
    they name symbols the code reaches without a name token."""
    out: set[str] = set()
    for k, t in enumerate(tokens):
        if t.text not in SYMBOL_STRING_WORDS:
            continue
        j = k + 1
        while j < len(tokens) and tokens[j].text in ('volatile', '__volatile__', 'goto', 'inline'):
            j += 1
        if j < len(tokens) and tokens[j].text == '(':
            try:
                args, _ = _arguments(tokens, j)
            except ExpansionError:
                continue
            for a in args:
                for y in a:
                    if y.kind == 'str':
                        out.update(IDENTIFIER.findall(y.text.split('"', 1)[-1]))
    return out


def _without_strings(text: str) -> str:
    return re.sub(r'"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'', ' ', text)


# ---- declared names -------------------------------------------------------------------------

def declared_names(tokens: list[Tok], macros: dict[str, list[Macro]]) -> tuple[set[str], bool]:
    """The names a declaration declares (generously: a name too many costs narrowness, a name
    missed would cost correctness), and whether it must always be in a closure."""
    if not tokens:
        return set(), False
    first = _skip_prefix(tokens)
    lead = tokens[first].text if first < len(tokens) else ''
    if any(t.text in ('_Pragma', '__pragma') or (t.kind == 'pp' and re.match(r'\s*#\s*pragma\b', t.text))
           for t in tokens):
        return set(), True          # a pragma (pack, optimize) acts on the declarations after it
    if (lead == 'static_assert' and tokens[-1].text == ';' and _single_call(tokens, first)
            and not any(t.text == '<' for t in tokens)):
        # a compile-time check without a template in it: it cannot change generated code (one
        # with a template argument list is kept: instantiating it could, in principle)
        return set(), False
    if lead == 'using' and not any(t.text == '=' for t in tokens):
        return set(), True          # using namespace / using-declaration: changes name lookup
    try:
        expanded = expand(tokens, macros)
    except ExpansionError:
        if any(m.pastes for t in tokens if t.kind == 'id' for m in macros.get(t.text, ())):
            raise
        expanded = tokens
    if any(t.text in ('_Pragma', '__pragma') for t in expanded):
        return set(), True
    names: set[str] = set()
    for toks in (tokens, expanded):
        n, always = _names(toks, macros)
        if always:
            return set(), True
        names |= n
    if lead in ('typedef', 'using', 'namespace', 'friend') or 'typedef' in _top_words(tokens):
        names |= identifiers(_outside_braces(tokens))
    names -= KEYWORDS
    if not names:
        return set(), True
    return names, False


def _skip_prefix(tokens: list[Tok]) -> int:
    """Index of the first token after template<...> heads and attributes."""
    i = 0
    while i < len(tokens):
        if tokens[i].text == 'template' and i + 1 < len(tokens) and tokens[i + 1].text == '<':
            depth, j = 0, i + 1
            while j < len(tokens):
                if tokens[j].text == '<':
                    depth += 1
                elif tokens[j].text == '>':
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            i = j + 1
        elif tokens[i].text == '[' and i + 1 < len(tokens) and tokens[i + 1].text == '[':
            j = i
            depth = 0
            while j < len(tokens):
                depth += {'[': 1, ']': -1}.get(tokens[j].text, 0)
                j += 1
                if depth == 0:
                    break
            i = j
        else:
            return i
    return i


def _single_call(tokens: list[Tok], first: int) -> bool:
    depth = 0
    for j in range(first + 1, len(tokens)):
        depth += {'(': 1, ')': -1}.get(tokens[j].text, 0)
        if depth == 0:
            return j == len(tokens) - 2
    return False


def _head_length(tokens: list[Tok]) -> int:
    """How many tokens come before the first top-level brace."""
    depth = 0
    for k, t in enumerate(tokens):
        if t.text in ('(', '['):
            depth += 1
        elif t.text in (')', ']'):
            depth -= 1
        elif t.text == '{' and depth == 0:
            return k
    return len(tokens)


def _top_words(tokens: list[Tok]) -> set[str]:
    return {t.text for t in _outside_braces(tokens)}


def _outside_braces(tokens: list[Tok]) -> list[Tok]:
    out, depth = [], 0
    for t in tokens:
        if t.text == '{':
            depth += 1
        elif t.text == '}':
            depth = max(0, depth - 1)
        elif depth == 0:
            out.append(t)
    return out


def _names(tokens: list[Tok], macros) -> tuple[set[str], bool]:
    names: set[str] = set()
    n = len(tokens)
    brace = paren = bracket = 0
    enum_body = False
    is_enum = False
    for i, t in enumerate(tokens):
        x = t.text
        nxt = tokens[i + 1].text if i + 1 < n else ''
        if x == '{':
            brace += 1
            continue
        if x == '}':
            brace = max(0, brace - 1)
            continue
        if x == '(':
            paren += 1
            continue
        if x == ')':
            paren = max(0, paren - 1)
            continue
        if x == '[':
            bracket += 1
            continue
        if x == ']':
            bracket = max(0, bracket - 1)
            continue
        if brace and is_enum and t.kind == 'id':
            names.add(x)                          # enumerators
            continue
        if brace:
            continue
        if x == 'operator':
            return set(), True                    # operator overloads are called without their name
        if x == 'enum':
            is_enum = True
        if t.kind != 'id' or x in KEYWORDS:
            if x in TAG_KEYWORDS:
                tag = _tag_name(tokens, i + 1)
                if tag:
                    names.add(tag)
            continue
        if bracket:
            if paren == 0 and _structured_binding(tokens, i):
                names.add(x)                      # auto [a, b] = ...
            continue
        if paren == 0:
            follower = _follower(tokens, i + 1)
            if follower == '<':
                follower = _after_template_args(tokens, _skip_attributes(tokens, i + 1))
            # a function-like macro's invocation is found through its definition, not as a declared
            # name; the same name used any other way is an ordinary declarator
            invoked = nxt == '(' and any(m.params is not None for m in macros.get(x, ()))
            if follower in DECLARATOR_FOLLOWERS and not invoked:
                names.add(x)
                names |= _qualifiers(tokens, i)
            # an unknown macro invocation in capitals at the top level may declare its arguments
            if nxt == '(' and (x in macros or re.fullmatch(r'[A-Z][A-Z0-9_]+', x)):
                try:
                    args, _ = _arguments(tokens, i + 1)
                except ExpansionError:
                    args = []
                for a in args:
                    ids = [y for y in a if y.kind == 'id' and y.text not in KEYWORDS]
                    if len(a) == 1 and ids:
                        names.add(ids[0].text)
                    if x not in macros:
                        names |= {y.text for y in ids}
        elif nxt in ('(', '[') and not (x in macros and nxt == '('):
            # inside parentheses at the top level, a name followed by ( or [ declares: a function
            # returning a function pointer, an array of function pointers (*name[N])(...)
            names.add(x)
            names |= _qualifiers(tokens, i)
        elif _parenthesized_declarator(tokens, i):
            names.add(x)                          # (name), (*name)(...), (&name)[N]
            names |= _qualifiers(tokens, i)
    return names, False


def _structured_binding(tokens: list[Tok], i: int) -> bool:
    j = i - 1
    while j >= 0 and (tokens[j].kind == 'id' and tokens[j].text not in KEYWORDS or tokens[j].text == ','):
        j -= 1
    if j < 0 or tokens[j].text != '[':
        return False
    j -= 1
    while j >= 0 and tokens[j].text in ('&', '&&', 'const', 'volatile', 'static', 'thread_local'):
        j -= 1
    return j >= 0 and tokens[j].text == 'auto'


ATTRIBUTE_WORDS = frozenset({'__attribute__', '__declspec', 'alignas', '__asm__', '__asm', 'asm'})


def _skip_attributes(tokens: list[Tok], j: int) -> int:
    """The index after any attributes starting at j (__attribute__((...)), [[...]], alignas(...),
    __declspec(...), asm("label"))."""
    n = len(tokens)
    while j < n:
        if tokens[j].text in ATTRIBUTE_WORDS and j + 1 < n and tokens[j + 1].text == '(':
            try:
                _, j = _arguments(tokens, j + 1)
            except ExpansionError:
                return n
        elif tokens[j].text == '[' and j + 1 < n and tokens[j + 1].text == '[':
            depth = 0
            while j < n:
                depth += {'[': 1, ']': -1}.get(tokens[j].text, 0)
                j += 1
                if depth == 0:
                    break
        else:
            return j
    return j


def _follower(tokens: list[Tok], j: int) -> str:
    j = _skip_attributes(tokens, j)
    return tokens[j].text if j < len(tokens) else ''


def _parenthesized_declarator(tokens: list[Tok], i: int) -> bool:
    """Whether tokens[i] is the name of a parenthesized declarator: `(name)`, `(*name)(...)`,
    `(Class::*name)`, `(&name)[N]`, followed by what follows a declarator."""
    j = i - 1
    while j >= 0 and tokens[j].text in ('*', '&', '&&', '^', 'const', 'volatile'):
        j -= 1
    if not _opens_declarator(tokens, j):
        return False
    k = _skip_attributes(tokens, i + 1)
    if k >= len(tokens) or tokens[k].text != ')':
        return False
    while k < len(tokens) and tokens[k].text == ')':
        k += 1
    after = _follower(tokens, k)
    return after in DECLARATOR_FOLLOWERS or after == ''


def _opens_declarator(tokens: list[Tok], j: int) -> bool:
    """Whether tokens[j] ends `(` or `(Class::` before the * of a (*name)(...) declarator."""
    while j >= 1 and tokens[j].text == '::' and tokens[j - 1].kind == 'id':
        j -= 2
    return j >= 0 and tokens[j].text == '('


def _tag_name(tokens: list[Tok], i: int) -> str:
    n = len(tokens)
    while i < n:
        x = tokens[i].text
        if x in ('class', 'struct', 'final', 'alignas', '__attribute__', '__declspec'):
            if x in ('alignas', '__attribute__', '__declspec') and i + 1 < n and tokens[i + 1].text == '(':
                try:
                    _, i = _arguments(tokens, i + 1)
                except ExpansionError:
                    return ''
                continue
            i += 1
            continue
        if x == '[' and i + 1 < n and tokens[i + 1].text == '[':
            depth = 0
            while i < n:
                depth += {'[': 1, ']': -1}.get(tokens[i].text, 0)
                i += 1
                if depth == 0:
                    break
            continue
        if tokens[i].kind == 'id' and x not in KEYWORDS:
            # a qualified name: take its last part (and the rest via the class's own name)
            while i + 2 < n and tokens[i + 1].text == '::' and tokens[i + 2].kind == 'id':
                i += 2
            return tokens[i].text
        return ''
    return ''


def _after_template_args(tokens: list[Tok], i: int) -> str:
    depth = 0
    for j in range(i, min(len(tokens), i + 200)):
        x = tokens[j].text
        if x == '<':
            depth += 1
        elif x == '>':
            depth -= 1
            if depth == 0:
                return tokens[j + 1].text if j + 1 < len(tokens) else ''
        elif x in (';', '{', '}'):
            return ''
    return ''


def _qualifiers(tokens: list[Tok], i: int) -> set[str]:
    """The classes and namespaces a declared name is qualified with (Outer::Inner::~name)."""
    out = set()
    j = i - 1
    if j >= 0 and tokens[j].text == '~':
        j -= 1
    while j >= 1 and tokens[j].text == '::':
        k = j - 1
        if tokens[k].text == '>':
            depth = 0
            while k >= 0:
                depth += {'>': 1, '<': -1}.get(tokens[k].text, 0)
                if depth == 0:
                    break
                k -= 1
            k -= 1
        if k < 0 or tokens[k].kind != 'id':
            break
        out.add(tokens[k].text)
        j = k - 1
    return out


# ---- the translation unit -------------------------------------------------------------------

_parse_cache: dict[bytes, list[Decl] | ParseError] = {}


def parsed(path: str) -> list[Decl]:
    with open(path, 'rb') as f:
        data = f.read()
    key = hashlib.sha256(data).digest()
    if key not in _parse_cache:
        try:
            _parse_cache[key] = parse(data.decode('utf-8', errors='replace'))
        except ParseError as e:
            _parse_cache[key] = e
    result = _parse_cache[key]
    if isinstance(result, ParseError):
        raise ParseError(f'{path}: {result}')
    return result


class Entry:
    """A declaration placed in a translation unit."""
    __slots__ = ('decl', 'file', 'conds', 'scope', 'position')

    def __init__(self, decl, file, conds, scope, position):
        self.decl, self.file, self.conds, self.scope, self.position = decl, file, conds, scope, position

    def text(self) -> str:
        return '\0'.join([self.file, '\n'.join(self.conds), '\n'.join(self.scope), self.decl.text])


def resolve(root: str, including: str, target: str) -> str | None:
    """The repository file a quoted include names, searched as verification.source_files does."""
    search = [os.path.dirname(including), os.path.join(root, 'game'), os.path.join(root, 'runtime', 'include')]
    for base in search:
        candidate = os.path.normpath(os.path.join(base, target))
        if os.path.isfile(candidate):
            return candidate
    return None


def guard_of(decls: list[Decl]) -> str | None:
    """A header's guard, if its content is read once however often it is included: '#pragma once'
    outside any #if, or the #ifndef G line around everything when its first line is #define G and
    the header never #undefs G. None for a header without a guard."""
    if not decls:
        return '#pragma once'
    if any(d.kind == 'pragma' and not d.conds and re.search(r'\bonce\b', d.text) for d in decls):
        return '#pragma once'
    first = decls[0]
    if first.kind != 'macro' or not first.conds:
        return None
    guard = first.conds[0]
    m = re.match(r'#\s*ifndef\s+(\w+)$', guard) or re.match(r'#\s*if\s+!\s*defined\s*\(?\s*(\w+)\s*\)?$', guard)
    if not (m and m.group(1) == first.name and all(d.conds[:1] == (guard,) for d in decls)):
        return None
    if any(d.kind == 'macro' and d.tokens[1].text == 'undef' and d.name == first.name for d in decls):
        return None
    return guard


def unit(root: str, defining: str) -> list[Entry]:
    """The translation unit of one defining file, in include order. A header's content is placed at
    its first include; an include site that matters by itself is an always-in marker: a second
    include of a header without a guard (its directives run again), and every include of a header
    that is included anywhere under conditional compilation (which include is live is then a
    question of the #if lines, so all of them count)."""
    entries: list[Entry] = []
    seen: set[str] = set()
    guard_names: dict[str, str] = {}      # header -> its #ifndef guard macro
    conditional: set[str] = set()         # headers with an include site under a real #if
    skipped: set[str] = set()             # guarded headers whose later include sites were skipped

    def marker(d: Decl, rel: str, c: tuple, sc: tuple) -> None:
        m = Decl('include', d.tokens, d.conds, d.scope, include=d.include, always=True)
        entries.append(Entry(m, rel, c, sc, len(entries)))

    def walk(path: str, conds: tuple, scope: tuple, guard_at: frozenset) -> None:
        # guard_at: the positions in conds that are the guards of the files being included, not
        # real conditions
        seen.add(path)
        decls = parsed(path)
        guard = guard_of(decls)
        own = frozenset()
        if guard and guard != '#pragma once':
            guard_names[path] = decls[0].name
            own = frozenset({len(conds)})
        rel = os.path.relpath(path, root).replace(os.sep, '/')
        for d in decls:
            c, sc = conds + d.conds, scope + d.scope
            if d.kind == 'include' and d.include and d.include[0] == 'quote':
                target = resolve(root, path, d.include[1])
                if target is not None:
                    target = os.path.normpath(target)
                    positions = guard_at | own
                    if any(i not in positions for i in range(len(c))):
                        conditional.add(target)
                    if target in seen:
                        if guard_of(parsed(target)) is None or target in conditional:
                            marker(d, rel, c, sc)
                        else:
                            skipped.add(target)
                        continue
                    if target in conditional:
                        marker(d, rel, c, sc)
                    walk(target, c, sc, guard_at | own)
                    continue
            entries.append(Entry(d, rel, c, sc, len(entries)))

    walk(os.path.normpath(os.path.join(root, defining)), (), (), frozenset())
    # A guard #undef'd elsewhere makes a skipped include live again: do not trust the reading.
    undone = {e.decl.name for e in entries if e.decl.kind == 'macro' and e.decl.tokens[1].text == 'undef'}
    if any(guard_names.get(h) in undone for h in skipped):
        raise ParseError('a header guard is #undef\'d and the header included again')
    return entries


class Closure:
    def __init__(self, entries: list[Entry], positional: bool):
        self.entries = entries
        self.positional = positional


class Index:
    """What the closure needs of one translation unit, whatever the root: the macros, each entry's
    declared names, the entries always taken, and the identifiers each entry names."""

    def __init__(self, entries: list[Entry]):
        self.macros: dict[str, list[Macro]] = {}
        for e in entries:
            if e.decl.kind == 'macro':
                self.macros.setdefault(e.decl.name, []).append(Macro(e.decl))
        self.by_name: dict[str, list[int]] = {}
        self.names: list[set[str]] = []
        self.always: list[int] = []
        self._ids: dict[int, set[str]] = {}
        self._users: dict[str, list[int]] | None = None
        self.entries = entries
        for k, e in enumerate(entries):
            d = e.decl
            names: set[str] = set()
            if d.kind == 'macro':
                names = {d.name}
            elif d.kind != 'code':
                if d.always or d.kind == 'include':
                    self.always.append(k)
            else:
                names, always = declared_names(d.tokens, self.macros)
                if always:
                    self.always.append(k)
            self.names.append(names)
            for name in names:
                self.by_name.setdefault(name, []).append(k)

    def ids(self, k: int) -> set[str]:
        """The identifiers entry k names: in its text, its macro expansion and the conditional and
        namespace lines around it."""
        if k not in self._ids:
            e = self.entries[k]
            toks = e.decl.tokens
            ids = identifiers(toks)
            try:
                ids |= identifiers(expand(toks, self.macros))
            except ExpansionError:
                if any(m.pastes for name in ids for m in self.macros.get(name, ())):
                    raise ParseError(f'{e.file}: a token-pasting macro could not be expanded')
            for c in e.conds + e.scope:
                ids |= set(re.findall(r'[A-Za-z_]\w*', c))
            ids |= _symbol_strings(toks)
            self._ids[k] = ids
        return self._ids[k]

    def internal(self, k: int) -> bool:
        """Whether entry k declares something with internal linkage: in an anonymous namespace, or
        `static` at namespace scope."""
        e = self.entries[k]
        if e.decl.kind != 'code':
            return False
        if any(re.fullmatch(r'(?:inline\s+)?namespace', h.strip()) for h in e.scope):
            return True
        return 'static' in _top_words(e.decl.tokens[:_head_length(e.decl.tokens)])

    def users(self, name: str) -> list[int]:
        """Every entry whose identifiers include `name`."""
        if self._users is None:
            self._users = {}
            for k in range(len(self.entries)):
                for n in self.ids(k):
                    self._users.setdefault(n, []).append(k)
        return self._users.get(name, [])


_index_cache: dict[tuple, Index] = {}


def index(entries: list[Entry]) -> Index:
    key = tuple((id(e.decl), e.file, e.conds, e.scope) for e in entries)
    if key not in _index_cache:
        if len(_index_cache) > 256:
            _index_cache.clear()
        _index_cache[key] = Index(entries)
    return _index_cache[key]


def closure(entries: list[Entry], is_root) -> Closure:
    """Every entry the root declarations reach by name (see the module docstring)."""
    ix = index(entries)
    roots = [k for k, names in enumerate(ix.names)
             if entries[k].decl.kind == 'code' and any(is_root(n) for n in names)]
    if not roots:
        raise ParseError('the replacement\'s definition was not found')
    queue = roots + ix.always
    taken: set[int] = set()
    done_names: set[str] = set()
    reverse_done: set[str] = set()
    positional = False
    while queue:
        k = queue.pop()
        if k in taken:
            continue
        taken.add(k)
        ids = ix.ids(k)
        if ids & POSITIONAL:
            positional = True
        for name in ids - done_names:
            done_names.add(name)
            queue.extend(ix.by_name.get(name, ()))
        # Internal linkage: the compiler sees every use of a static or anonymous-namespace entity
        # in this unit, and how it compiles the function can depend on them (a static never
        # written folds to a constant; a static function called once is inlined; a class nobody
        # derives from is devirtualized). So every declaration that names one comes in too.
        if ix.internal(k):
            for name in ix.names[k]:
                if name not in reverse_done:
                    reverse_done.add(name)
                    queue.extend(ix.users(name))
    return Closure([entries[k] for k in sorted(taken)], positional)


def fingerprint(root: str, defining: list[str], is_root) -> str | None:
    """The f1: fingerprint over the defining files' translation units, or None where the reading
    cannot be trusted (the caller then uses the legacy hash)."""
    if not defining:
        return None
    h = hashlib.sha256(VERSION)
    try:
        for rel in sorted(defining):
            c = closure(unit(root, rel), is_root)
            if c.positional:
                return None
            h.update(rel.encode() + b'\0\1')
            for e in c.entries:
                h.update(e.text().encode() + b'\0\2')
    except (ParseError, ExpansionError, RecursionError):
        return None
    return PREFIX + h.hexdigest()[:HASH_LENGTH]


def root_matcher(name: str, family: bool = False):
    if family:
        pattern = re.compile(rf'bb_{re.escape(name)}(?:_\w+)?')
        return lambda n: pattern.fullmatch(n) is not None
    target = f'bb_{name}'
    return lambda n: n == target


def explain(root: str, defining: list[str], is_root) -> list[str]:
    """The closure as `file: first line` lines, for --explain."""
    out = []
    for rel in sorted(defining):
        c = closure(unit(root, rel), is_root)
        out.append(f'# {rel}' + (' (positional macros: legacy hash)' if c.positional else ''))
        for e in c.entries:
            out.append(f'{e.file}: {e.decl.text[:110]}')
    return out


def main() -> int:
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import verification
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('name', nargs='?')
    ap.add_argument('--family', action='store_true')
    ap.add_argument('--root', default=os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument('--explain', action='store_true', help='list the declarations in the closure')
    ap.add_argument('--stats', action='store_true', help='count functions with rows that narrow')
    ap.add_argument('--migrate', action='store_true',
                    help='rewrite legacy source values that are current to the fingerprint of the same code')
    a = ap.parse_args()
    if a.migrate:
        import families
        changed = verification.migrate(a.root, os.path.join(a.root, 'symbols', 'verification.csv'))
        print(f'symbols/verification.csv: {changed} row(s) now carry the fingerprint')
        for fam in families.families(a.root):
            path = families.verification_path(a.root, fam['family'])
            if os.path.isfile(path):
                name = f"{fam['system']}_{fam['family']}"
                n = verification.migrate(a.root, path, name_of=lambda r, name=name: name, family=True)
                print(f'{os.path.relpath(path, a.root)}: {n} row(s) now carry the fingerprint')
        return 0
    if a.stats:
        rows = verification.read_csv(os.path.join(a.root, 'symbols', 'verification.csv'),
                                     verification.VERIFICATION_HEADER)
        names = sorted({r['function'] for r in rows})
        legacy = [n for n in names if not verification.source_fingerprint(a.root, n).startswith(PREFIX)]
        print(f'{len(names) - len(legacy)} of {len(names)} functions with rows have a fingerprint; '
              f'{len(legacy)} use the legacy hash')
        for n in legacy:
            print(f'  legacy: {n}')
        return 0
    if not a.name:
        ap.error('NAME is required')
    print(verification.source_fingerprint(a.root, a.name, a.family))
    if a.explain:
        files = verification.defining_files(a.root, a.name, a.family)
        try:
            print('\n'.join(explain(a.root, files, root_matcher(a.name, a.family))))
        except (ParseError, ExpansionError) as e:
            print(f'legacy hash: {e}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
