# SPDX-License-Identifier: GPL-2.0-or-later
"""tools/harness.py and verify.py compare on a synthetic image: no game data needed.

The image holds one function at offset 0 (PS4 address 0x400000):

    f(out): calls import 0 (via its GOT slot) with (7, &local); local += *glob_ptr;
            *out = local; glob2 = (uint32)local

Data page at 0x1000: got0 (import 0), glob = 0x100, glob_ptr -> glob (base-relative relocation),
glob2. A correct native replacement must give the same result, a broken one must not.
"""
import base64
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, 'tools')
sys.path.insert(0, TOOLS)
import verify  # noqa: E402

CODE = bytes.fromhex('534889fb4883ec10bf070000004889e6ff15ea0f0000488b0424488b0def0f0000'
                     '4803014889038905eb0f00004883c4105bc3')
# At 0x400800: return [rsp+8] + [rsp+16] + [rsp+24] (three stack arguments).
STACK_CODE = bytes.fromhex('488b442408' '4803442410' '4803442418' 'c3')
DATA = bytes.fromhex('0000000000000000000100000000000008100000000000000000000000000000')
SIZE = 0x1020

REPLACEMENT_C = r'''
#include <stdint.h>
typedef struct { uint32_t version; unsigned char *image; uint64_t image_size;
                 int (*install_hook)(uint64_t, uint64_t, const void *); } host_t;
static unsigned char *img;
int bbgame_init(const host_t *h) { img = h->image; return 0; }
typedef int (*import_fn)(int, uint64_t *);
void replacement(uint64_t *out)
{
    uint64_t local;
    (*(import_fn *)(img + 0x1000))(7, &local);
    local += **(uint64_t **)(img + 0x1010);
    BRANCH
    OUT_WRITE
    GLOBAL_WRITE
}
'''


def nid(name):
    digest = hashlib.sha1(name.encode() + bytes.fromhex('518d64a635ded8c1e6b039b1c3e55230')).digest()[:8][::-1]
    return base64.b64encode(digest).decode().rstrip('=').replace('/', '-')


def boot_image():
    """The synthetic executable: code (with the stack-argument function at 0x800) and a data page
    whose first word is the import's slot and whose word at +0x10 points at +0x8."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ps4elf
    code = bytearray(0x1000)
    code[:len(CODE)] = CODE
    code[0x800:0x800 + len(STACK_CODE)] = STACK_CODE
    code[0x900:0x906] = bytes.fromhex('b807000000c3')
    code[0x910:0x912] = bytes.fromhex('ebfe')
    code[0x920:0x928] = bytes.fromhex('48c7c707000000c3')
    code[0x930:0x936] = bytes.fromhex('ba09000000c3')
    # Deliberately writes one byte beyond an eight-byte declared buffer. The payload remains
    # mapped, so the deterministic redzone check must report the exact overflow offset.
    code[0x940:0x945] = bytes.fromhex('c6470801c3')
    # Explicit guest memory operands for the instruction tracer: load then a same-value store.
    code[0x950:0x955] = bytes.fromhex('8b07 8906 c3')
    # One-byte out-of-bounds read and same-value write into the mapped redzone.
    code[0x960:0x969] = bytes.fromhex('0fb64704 884704 c3')
    # Unaligned 32-bit read, followed by return.
    code[0x970:0x976] = bytes.fromhex('8b4701 c3')
    # Aliased and overlapping pointer test: read two bytes, then write two at rsi.
    code[0x980:0x987] = bytes.fromhex('66 8b07 66 8906 c3')
    code[0x990:0x993] = bytes.fromhex('8b07 c3')
    # POP memory destinations use the incremented RSP. Save the real return stack, point RSP
    # at a fixture buffer, pop into an RSP-relative destination, restore RSP, then return.
    code[0x9a0:0x9a0 + 13] = bytes.fromhex('48 89e0 48 89fc 8f0424 4889c4 c3')
    code[0x9b0:0x9b0 + 14] = bytes.fromhex('48 89e0 48 89fc 8f442408 4889c4 c3')
    # SIB: [rsp + rcx*8 + 8], with rcx=1, so destination is entry RSP + 24.
    code[0x9c0:0x9c0 + 19] = bytes.fromhex('48 89e0 48 89fc b901000000 8f44cc08 4889c4 c3')
    # PUSH [rsp] reads from pre-decrement RSP and stores at RSP-8. Used to guard the
    # neighboring stack-instruction semantics while POP's special case is corrected.
    code[0x9e0:0x9e0 + 17] = bytes.fromhex('48 89e0 48 83c708 48 89fc ff3424 4889c4 c3')
    # Unsupported-memory fixtures stop before execution and must produce actionable gaps.
    code[0xa00:0xa02] = bytes.fromhex('a4c3')                         # movsb
    code[0xa10:0xa13] = bytes.fromhex('f3a4c3')                       # rep movsb
    code[0xa20:0xa23] = bytes.fromhex('f2aec3')                       # repne scasb
    code[0xa28:0xa2b] = bytes.fromhex('f3a6c3')                       # repe cmpsb
    code[0xa30:0xa35] = bytes.fromhex('c5fe6f07c3')                   # vmovdqu ymm0, [rdi]
    code[0xa40:0xa47] = bytes.fromhex('62f17f086f07c3')               # evex vmovdqu8
    code[0xa50:0xa53] = bytes.fromhex('0f05c3')                       # syscall
    code[0xa60:0xa63] = bytes.fromhex('d7c3')                         # xlatb
    code[0xa70:0xa74] = bytes.fromhex('0fae07c3')                      # fxsave [rdi]
    code[0xa80:0xa85] = bytes.fromhex('f00fc107c3')                   # lock xadd [rdi], eax
    code[0xa90:0xa9e] = bytes.fromhex('4889e2 4889fc 6650 6658 4889d4 c3')
    code[0xab0:0xab5] = bytes.fromhex('f20f1007c3')                   # scalar SSE movsd xmm0, [rdi]
    code[0xac0:0xac5] = bytes.fromhex('660ff7c1c3')                   # maskmovdqu [rdi], xmm1 implicit
    code[0xad0:0xad6] = bytes.fromhex('660f38f807c3')                 # movdir64b: implicit dest via RAX
    code[0xae0:0xae4] = bytes.fromhex('0f1c07c3')                     # cldemote [rdi], unclassified access
    code[0xaf0:0xaf3] = bytes.fromhex('0fa0c3')                       # push fs, width is not inferred
    code[0xaf8:0xafb] = bytes.fromhex('0fa1c3')                       # pop fs changes segment state
    code[0xb00:0xb05] = bytes.fromhex('c5f9f7c1c3')                   # vmaskmovdqu uses implicit RDI
    code[0xb10:0xb15] = bytes.fromhex('480fc70fc3')                   # cmpxchg16b [rdi], implicit atomic
    code[0xb20:0xb26] = bytes.fromhex('f3480faec7c3')                 # rdfsbase rdi, changes address state
    code[0xb30:0xb34] = bytes.fromhex('0fc7f0c3')                     # rdrand eax, nondeterministic
    code[0xb40:0xb46] = bytes.fromhex('f3480faee8c3')                 # incsspq rax, implicit shadow stack
    return ps4elf.build(
        segments=[(0, bytes(code), 0x1000, 5), (0x1000, DATA, 0x20, 6)],
        symbols=[(nid('testImport') + '#A#A', ps4elf.STT_FUNC, None)],
        relocations=[(0x1010, ps4elf.R_X86_64_RELATIVE, 0, 0x1008)],
        jump_slots=[(0x1000, ps4elf.R_X86_64_JUMP_SLOT, 1, 0)])


CASE = {
    'schema': 1, 'address': '0x00400000', 'id': 'synthetic', 'returns': 'void',
    'args': {'rdi': 'buf:out'},
    'buffers': {'out': {'size': 8}},
    'imports': [{'name': 'testImport', 'argc': 2, 'ret': 0,
                 'writes': [{'arg': 1, 'offset': 0, 'bytes': struct.pack('<Q', 0x1234).hex()}]}],
}


@unittest.skipUnless(sys.platform == 'linux' and shutil.which(os.environ.get('CC', 'cc')),
                     'needs Linux x86-64 and a C compiler')
class Harness(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.boot = os.path.join(self.d, 'eboot.elf')
        with open(self.boot, 'wb') as f:
            f.write(boot_image())
        self.case = os.path.join(self.d, 'case.json')
        with open(self.case, 'w') as f:
            json.dump(CASE, f)

    def tearDown(self):
        shutil.rmtree(self.d)

    def side(self, lib=None):
        return self.side_case(self.case, lib, '0x00400000')

    def side_case(self, case_path, lib=None, address='0x00400000'):
        cmd = [sys.executable, os.path.join(TOOLS, 'harness.py'), '--elf', self.boot,
               '--case', case_path, '--address', address]
        if lib:
            cmd += ['--replacement', 'replacement', '--lib', lib]
        if getattr(self, 'trace_instructions', False):
            cmd += ['--trace-instructions', '--trace-instruction-limit', '10000']
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        self.assertEqual(r.returncode, 0, r.stderr)
        return json.loads(r.stdout)

    def traced_case(self, case, address):
        path = os.path.join(self.d, f'trace-{address}.json')
        with open(path, 'w') as stream:
            json.dump(case, stream)
        self.trace_instructions = True
        try:
            return self.side_case(path, address=address)
        finally:
            self.trace_instructions = False

    def test_instruction_trace_records_unaligned_read_and_same_value_write(self):
        case = {'schema': 1, 'address': '0x00400950', 'id': 'read-write', 'returns': 'void',
                'args': {'rdi': 'buf:source', 'rsi': 'buf:output'},
                'buffers': {'source': {'size': 4, 'bytes': '78563412'},
                            'output': {'size': 8, 'bytes': '7856341200000000'}},
                'memory': [], 'imports': [], 'stubs': []}
        result = self.traced_case(case, '0x00400950')
        trace = result['instruction_trace']
        self.assertTrue(trace['complete'], trace)
        buffer_events = [event for event in trace['events'] if event['region'].startswith('buf:')]
        self.assertEqual([(event['access'], event['width']) for event in buffer_events],
                         [('read', 4), ('write', 4)])
        self.assertEqual(buffer_events[0]['region'], 'buf:source')
        self.assertEqual(buffer_events[1]['before'], buffer_events[1]['after'])
        self.assertTrue(buffer_events[1]['valid_region'])
        stack_event = next(event for event in trace['events'] if event['region'] == 'host_stack')
        self.assertEqual((stack_event['access'], stack_event['width']), ('read', 8))

    def test_instruction_trace_marks_one_byte_out_of_bounds_read_and_write(self):
        case = {'schema': 1, 'address': '0x00400960', 'id': 'oob', 'returns': 'void',
                'args': {'rdi': 'buf:source', 'rsi': 'buf:output'},
                'buffers': {'source': {'size': 4, 'bytes': 'aabbccdd'},
                            'output': {'size': 4, 'bytes': '00000000'}},
                'memory': [], 'imports': [], 'stubs': []}
        result = self.traced_case(case, '0x00400960')
        events = [event for event in result['instruction_trace']['events'] if event['region'] != 'host_stack']
        self.assertEqual([(event['access'], event['width'], event['valid_region'])
                          for event in events], [('read', 1, False), ('write', 1, False)])
        self.assertEqual(events[0]['region'], 'unmapped_or_undeclared')
        self.assertEqual(events[1]['before'], events[1]['after'])

    def test_instruction_trace_tracks_aliasing_and_unaligned_access(self):
        case = {'schema': 1, 'address': '0x00400970', 'id': 'unaligned', 'returns': 'i32',
                'args': {'rdi': 'buf:shared+0x1'},
                'buffers': {'shared': {'size': 8, 'bytes': '00 78563412 000000'.replace(' ', '')}},
                'memory': [], 'imports': [], 'stubs': []}
        result = self.traced_case(case, '0x00400970')
        event = next(event for event in result['instruction_trace']['events'] if event['region'] == 'buf:shared')
        self.assertEqual((event['access'], event['width'], event['region_offset']), ('read', 4, '+0x2'))
        alias = dict(case, address='0x00400980', id='alias', returns='void',
                     args={'rdi': 'buf:shared', 'rsi': 'buf:shared'})
        aliased = [event for event in self.traced_case(alias, '0x00400980')['instruction_trace']['events']
                   if event['region'] == 'buf:shared']
        self.assertEqual([(event['access'], event['width']) for event in aliased],
                         [('read', 2), ('write', 2)])
        self.assertEqual(aliased[0]['address'], aliased[1]['address'])
        overlapping = dict(alias, id='overlap', args={'rdi': 'buf:shared', 'rsi': 'buf:shared+0x1'})
        overlapped = [event for event in self.traced_case(overlapping, '0x00400980')['instruction_trace']['events']
                      if event['region'] == 'buf:shared']
        self.assertEqual([event['region_offset'] for event in overlapped], ['+0x0', '+0x1'])

    def _rsp_fixture(self, address, size, args=None):
        case = {'schema': 1, 'address': address, 'id': f'pop-rsp-{address}', 'returns': 'void',
                'args': {'rdi': 'buf:stack', **(args or {})},
                'buffers': {'stack': {'size': size, 'bytes': '8877665544332211' + '00' * (size - 8)}},
                'memory': [], 'imports': [], 'stubs': []}
        return self.traced_case(case, address)

    def _stack_buffer_events(self, result):
        return [event for event in result['instruction_trace']['events']
                if event['region'] == 'buf:stack']

    def _unsupported_instruction_case(self, address, args=None):
        case = {'schema': 1, 'address': address, 'id': f'unsupported-{address}', 'returns': 'void',
                'args': args or {'rdi': 'buf:memory', 'rsi': 'buf:source', 'rcx': 1},
                'buffers': {'memory': {'size': 128}, 'source': {'size': 128}},
                'memory': [], 'imports': [], 'stubs': []}
        return self.traced_case(case, address)

    def _assert_incomplete_instruction(self, address, capability, mnemonic):
        result = self._unsupported_instruction_case(address)
        trace = result['instruction_trace']
        self.assertFalse(trace['complete'], trace)
        self.assertEqual(trace['stop_reason'], 'unsupported_instruction')
        self.assertEqual(trace['instructions'], 0, 'unsupported instruction must not execute')
        gap = trace['gaps'][0]
        self.assertEqual(gap['mnemonic'], mnemonic)
        self.assertEqual(gap['capability'], capability)
        self.assertIn('0x', gap['instruction'])
        compared = verify.compare(
            {'schema': 1, 'address': address, 'cases': [{'id': 'same', **result}]},
            {'schema': 1, 'address': address, 'cases': [{'id': 'same', **result}]})
        self.assertEqual(compared['failed'], 1)
        self.assertIn(gap['instruction'], compared['failures'][0]['diffs'][0])
        self.assertIn(mnemonic, compared['failures'][0]['diffs'][0])
        self.assertIn(capability, compared['failures'][0]['diffs'][0])
        self.assertIn(gap['reason'], compared['failures'][0]['diffs'][0])
        return trace

    def test_pop_rsp_uses_post_increment_destination_and_pre_increment_stack_read(self):
        result = self._rsp_fixture('0x004009a0', 16)
        events = self._stack_buffer_events(result)
        self.assertTrue(result['instruction_trace']['complete'], result['instruction_trace'])
        self.assertEqual([(event['access'], event['region_offset'], event['width']) for event in events],
                         [('read', '+0x0', 8), ('write', '+0x8', 8)])
        self.assertEqual(events[1]['before'], '0000000000000000')
        self.assertEqual(events[1]['after'], '8877665544332211')
        self.assertTrue(all(event['valid_region'] for event in events))

    def test_pop_rsp_displacement_uses_incremented_stack_pointer(self):
        result = self._rsp_fixture('0x004009b0', 24)
        events = self._stack_buffer_events(result)
        self.assertTrue(result['instruction_trace']['complete'], result['instruction_trace'])
        self.assertEqual([(event['access'], event['region_offset']) for event in events],
                         [('read', '+0x0'), ('write', '+0x10')])
        self.assertTrue(events[1]['valid_region'])

    def test_pop_rsp_sib_index_and_displacement_use_incremented_base(self):
        result = self._rsp_fixture('0x004009c0', 32)
        events = self._stack_buffer_events(result)
        self.assertTrue(result['instruction_trace']['complete'], result['instruction_trace'])
        self.assertEqual([(event['access'], event['region_offset']) for event in events],
                         [('read', '+0x0'), ('write', '+0x18')])
        self.assertTrue(events[1]['valid_region'])

    def test_pop_rsp_actual_out_of_bounds_destination_fails_closed(self):
        result = self._rsp_fixture('0x004009a0', 8)
        trace = result['instruction_trace']
        events = [event for event in result['instruction_trace']['events']
                  if event['region'] == 'buf:stack' or event.get('bounds_violation')]
        self.assertFalse(trace['complete'], trace)
        self.assertEqual([(event['access'], event['region'], event['valid_region'])
                          for event in events], [('read', 'buf:stack', True),
                                                ('write', 'unmapped_or_undeclared', False)])
        # Before this fix both events were attributed to +0x0, making the destination look in-bounds.
        self.assertEqual(int(events[1]['address'], 16), int(events[0]['address'], 16) + 8)
        compared = verify.compare(
            {'schema': 1, 'address': '0x004009a0', 'cases': [result]},
            {'schema': 1, 'address': '0x004009a0', 'cases': [result]})
        self.assertEqual(compared['failed'], 1)
        self.assertTrue(any('instruction memory bounds violation' in diff
                            for diff in compared['failures'][0]['diffs']))

    def test_push_rsp_source_keeps_pre_decrement_address(self):
        result = self._rsp_fixture('0x004009e0', 16)
        events = self._stack_buffer_events(result)
        self.assertTrue(result['instruction_trace']['complete'], result['instruction_trace'])
        # The source uses pre-decrement RSP; the implicit destination is one word below it.
        self.assertEqual([(event['access'], event['region_offset']) for event in events[:2]],
                         [('read', '+0x8'), ('write', '+0x0')])

    def test_string_and_repeat_memory_forms_fail_closed_with_diagnostics(self):
        self._assert_incomplete_instruction('0x00400a00', 'string_memory', 'movsb')
        self._assert_incomplete_instruction('0x00400a10', 'repeat_string_memory', 'rep movsb')
        self._assert_incomplete_instruction('0x00400a20', 'repeat_string_memory', 'repne scasb')
        self._assert_incomplete_instruction('0x00400a28', 'repeat_string_memory', 'repe cmpsb')

    def test_vex_and_evex_memory_operands_fail_before_execution(self):
        self._assert_incomplete_instruction('0x00400a30', 'vex_evex_memory', 'vmovdqu')
        self._assert_incomplete_instruction('0x00400a40', 'vex_evex_memory', 'vmovdqu8')

    def test_system_and_special_implicit_memory_operations_fail_closed(self):
        self._assert_incomplete_instruction('0x00400a50', 'system_instruction', 'syscall')
        self._assert_incomplete_instruction('0x00400a60', 'special_memory_operation', 'xlatb')
        self._assert_incomplete_instruction('0x00400a70', 'special_memory_operation', 'fxsave')
        self._assert_incomplete_instruction('0x00400a80', 'atomic_memory', 'lock xadd')
        self._assert_incomplete_instruction('0x00400ac0', 'special_memory_operation', 'maskmovdqu')
        self._assert_incomplete_instruction('0x00400ad0', 'special_memory_operation', 'movdir64b')
        self._assert_incomplete_instruction('0x00400ae0', 'special_memory_operation', 'cldemote')
        self._assert_incomplete_instruction('0x00400af0', 'stack_segment_state', 'push')
        self._assert_incomplete_instruction('0x00400af8', 'stack_segment_state', 'pop')
        self._assert_incomplete_instruction('0x00400b00', 'special_memory_operation', 'vmaskmovdqu')
        self._assert_incomplete_instruction('0x00400b10', 'atomic_memory', 'cmpxchg16b')
        self._assert_incomplete_instruction('0x00400b20', 'system_instruction', 'rdfsbase')
        self._assert_incomplete_instruction('0x00400b30', 'nondeterministic_instruction', 'rdrand')
        self._assert_incomplete_instruction('0x00400b40', 'system_instruction', 'incsspq')

    def test_16_bit_register_push_and_pop_trace_exact_stack_width(self):
        case = {'schema': 1, 'address': '0x00400a90', 'id': 'stack-width-16', 'returns': 'void',
                'args': {'rdi': 'buf:stack+0x2'},
                'buffers': {'stack': {'size': 4, 'bytes': '00000000'}},
                'memory': [], 'imports': [], 'stubs': []}
        result = self.traced_case(case, '0x00400a90')
        events = self._stack_buffer_events(result)
        self.assertTrue(result['instruction_trace']['complete'], result['instruction_trace'])
        self.assertEqual([(event['access'], event['width'], event['region_offset']) for event in events],
                         [('write', 2, '+0x0'), ('read', 2, '+0x0')])

    def test_scalar_sse_movsd_memory_operand_is_not_misclassified_as_string(self):
        case = {'schema': 1, 'address': '0x00400ab0', 'id': 'scalar-movsd', 'returns': 'void',
                'args': {'rdi': 'buf:memory'},
                'buffers': {'memory': {'size': 8, 'bytes': '8877665544332211'}},
                'memory': [], 'imports': [], 'stubs': []}
        result = self.traced_case(case, '0x00400ab0')
        self.assertTrue(result['instruction_trace']['complete'], result['instruction_trace'])
        events = [event for event in result['instruction_trace']['events']
                  if event['region'] == 'buf:memory']
        self.assertEqual([(event['access'], event['width']) for event in events], [('read', 8)])

    def test_first_divergence_diagnostic_allows_equivalent_instruction_paths(self):
        case = {'schema': 1, 'address': '0x00400990', 'id': 'different-path', 'returns': 'i32',
                'args': {'rdi': 'buf:value'}, 'buffers': {'value': {'size': 4, 'bytes': '78563412'}},
                'memory': [], 'imports': [], 'stubs': []}
        path = os.path.join(self.d, 'different-path.json')
        with open(path, 'w') as stream:
            json.dump(case, stream)
        replacement_source = os.path.join(self.d, 'different-path.c')
        replacement_lib = os.path.join(self.d, 'different-path.so')
        with open(replacement_source, 'w') as stream:
            stream.write('#include <stdint.h>\n'
                         'typedef struct { uint32_t version; unsigned char *image; uint64_t image_size;\n'
                         'int (*install_hook)(uint64_t,uint64_t,const void *); } host_t;\n'
                         'int bbgame_init(const host_t *host) { (void)host; return 0; }\n'
                         'uint32_t replacement(volatile uint32_t *value) { uint32_t first=*value; '
                         '(void)*value; return first; }\n')
        subprocess.run([os.environ.get('CC', 'cc'), '-shared', '-fPIC', '-O2', '-o', replacement_lib,
                        replacement_source], check=True)
        self.trace_instructions = True
        try:
            original = self.side_case(path, address='0x00400990')
            replacement = self.side_case(path, replacement_lib, '0x00400990')
        finally:
            self.trace_instructions = False
        compared = verify.compare(
            {'schema': 1, 'address': '0x00400990', 'cases': [original]},
            {'schema': 1, 'address': '0x00400990', 'cases': [replacement]})
        self.assertEqual(compared['failed'], 0)
        divergence = compared['instruction_diagnostics'][0]['first_divergence']
        self.assertEqual(divergence['classification'], 'bounded_memory_trace_difference')
        self.assertFalse(divergence['observable_result_mismatch'])
        self.assertIn('state_before', divergence['replacement'])

        with open(replacement_source, 'w') as stream:
            stream.write('#include <stdint.h>\n'
                         'typedef struct { uint32_t version; unsigned char *image; uint64_t image_size;\n'
                         'int (*install_hook)(uint64_t,uint64_t,const void *); } host_t;\n'
                         'int bbgame_init(const host_t *host) { (void)host; return 0; }\n'
                         'uint32_t replacement(volatile uint32_t *value) { return *value + 1; }\n')
        subprocess.run([os.environ.get('CC', 'cc'), '-shared', '-fPIC', '-O2', '-o', replacement_lib,
                        replacement_source], check=True)
        self.trace_instructions = True
        try:
            mutated = self.side_case(path, replacement_lib, '0x00400990')
        finally:
            self.trace_instructions = False
        rejected = verify.compare(
            {'schema': 1, 'address': '0x00400990', 'cases': [original]},
            {'schema': 1, 'address': '0x00400990', 'cases': [mutated]})
        self.assertEqual(rejected['failed'], 1)
        mismatch = rejected['instruction_diagnostics'][0]['first_divergence']
        self.assertTrue(mismatch['observable_result_mismatch'])

    def build(self, broken=False, mutation=None):
        src = os.path.join(self.d, 'r.c')
        with open(src, 'w') as f:
            source = REPLACEMENT_C.replace('BRANCH', 'local = local == 0x1334 ? 1 : local;' if mutation == 'branch' else '')
            source = source.replace('OUT_WRITE', '' if mutation == 'missing-write' else '*out = local ' + ('+ 1' if broken else '') + ';')
            source = source.replace('GLOBAL_WRITE',
                                    '*(uint16_t *)(img + 0x1018) = (uint16_t)local;' if mutation == 'narrow-write'
                                    else '*(uint32_t *)(img + 0x1018) = (uint32_t)local;')
            f.write(source)
        lib = os.path.join(self.d, f'r{int(broken)}-{mutation or "plain"}.so')
        subprocess.run([os.environ.get('CC', 'cc'), '-shared', '-fPIC', '-O2', '-o', lib, src], check=True)
        return lib

    def test_original_runs(self):
        r = self.side()
        self.assertEqual(r['calls'], [{'import': 'testImport', 'args': ['0x7', 'stack']}])
        self.assertIn({'addr': 'buf:out+0x0', 'bytes': '3413'}, r['writes'])
        self.assertIn({'addr': '0x00401018', 'bytes': '3413'}, r['writes'])
        self.assertTrue(all(v == 'kept' for v in r['preserved'].values()))
        self.assertTrue(any(event['access'] == 'write' and event['source'] == 'import_write'
                            for event in r['access_trace']))
        self.assertIn('not instrumented', r['access_trace_scope'])

    def test_byte_precise_redzone_reports_guest_buffer_overrun(self):
        case = {'schema': 1, 'address': '0x00400940', 'id': 'redzone', 'returns': 'void',
                'args': {'rdi': 'buf:out'}, 'buffers': {'out': {'size': 8}},
                'memory': [], 'imports': [], 'stubs': []}
        path = os.path.join(self.d, 'redzone.json')
        with open(path, 'w') as stream:
            json.dump(case, stream)
        result = self.side_case(path, address='0x00400940')
        self.assertEqual(result['redzone_writes'], [{'buffer': 'out', 'first_offset': 8, 'changed_bytes': 1}])
        self.assertTrue(any('redzone modified' in error for error in result['errors']))

    def compare(self, broken):
        res = lambda r: {'schema': 1, 'address': '0x00400000', 'cases': [r]}
        return verify.compare(res(self.side()), res(self.side(self.build(broken))))

    def test_correct_replacement_matches(self):
        self.assertEqual(self.compare(False)['failed'], 0)

    def test_broken_replacement_fails(self):
        out = self.compare(True)
        self.assertEqual(out['failed'], 1)
        self.assertIn('buf:out+0x0', out['failures'][0]['diffs'][0])

    def test_mutation_matrix_rejects_branch_and_memory_write_errors(self):
        out = self.compare(False)
        self.assertEqual(out['failed'], 0)
        for mutation in ('branch', 'missing-write', 'narrow-write'):
            with self.subTest(mutation=mutation):
                case_path = self.case
                if mutation == 'narrow-write':
                    case = dict(CASE, memory=[{'addr': '0x00401018', 'bytes': 'aabbccdd'}])
                    case_path = os.path.join(self.d, 'narrow.json')
                    with open(case_path, 'w') as stream:
                        json.dump(case, stream)
                original = self.side_case(case_path)
                replacement = self.side_case(case_path, self.build(mutation=mutation))
                summary = verify.compare(
                    {'schema': 1, 'address': '0x00400000', 'cases': [original]},
                    {'schema': 1, 'address': '0x00400000', 'cases': [replacement]},
                )
                self.assertEqual(summary['failed'], 1)

    def test_wrong_return_mutation_is_detected_on_original_instructions(self):
        case = {'schema': 1, 'address': '0x00400900', 'id': 'return', 'returns': 'i32',
                'args': {}, 'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}
        case_path = os.path.join(self.d, 'return.json')
        with open(case_path, 'w') as stream:
            json.dump(case, stream)
        source = os.path.join(self.d, 'return.c')
        library = os.path.join(self.d, 'return.so')
        with open(source, 'w') as stream:
            stream.write('#include <stdint.h>\n'
                         'typedef struct { uint32_t version; unsigned char *image; uint64_t image_size;\n'
                         'int (*install_hook)(uint64_t,uint64_t,const void *); } host_t;\n'
                         'int bbgame_init(const host_t *h) { (void)h; return 0; }\n'
                         'int replacement(void) { return 8; }\n')
        subprocess.run([os.environ.get('CC', 'cc'), '-shared', '-fPIC', '-O2', '-o', library, source], check=True)
        original = verify.run_side(self.boot, case_path, '0x00400900', None, None)
        replacement = verify.run_side(self.boot, case_path, '0x00400900', 'replacement', library)
        summary = verify.compare(
            {'schema': 1, 'address': case['address'], 'cases': [original]},
            {'schema': 1, 'address': case['address'], 'cases': [replacement]},
        )
        self.assertEqual(summary['failed'], 1)
        self.assertTrue(any('ret.rax' in item for item in summary['failures'][0]['diffs']))

    def test_opted_in_caller_saved_register_is_compared(self):
        case = {'schema': 1, 'address': '0x00400920', 'id': 'caller-saved', 'returns': 'void',
                'observe_registers': ['rdi'], 'args': {}, 'buffers': {}, 'memory': [],
                'imports': [], 'stubs': []}
        case_path = os.path.join(self.d, 'register.json')
        with open(case_path, 'w') as stream:
            json.dump(case, stream)
        original = self.side_case(case_path, address='0x00400920')
        self.assertEqual(original['registers']['rdi'], '0x7')
        self.assertEqual(original['observed_registers'], ['rdi'])
        case.update(address='0x00400930', observe_registers=['rdx'])
        with open(case_path, 'w') as stream:
            json.dump(case, stream)
        original = self.side_case(case_path, address='0x00400930')
        self.assertEqual(original['registers']['rdx'], '0x9')

    def test_original_instruction_timeout_is_bounded(self):
        case = {'schema': 1, 'address': '0x00400910', 'id': 'infinite-loop', 'returns': 'void',
                'args': {}, 'buffers': {}, 'memory': [], 'imports': [], 'stubs': []}
        case_path = os.path.join(self.d, 'timeout.json')
        with open(case_path, 'w') as stream:
            json.dump(case, stream)
        result = verify.run_side(self.boot, case_path, case['address'], None, None, timeout=1)
        self.assertEqual(result['fault'], 'timeout after 1 s')

    def test_run_counts_cases_with_repeated_ids(self):
        caps = os.path.join(self.d, 'caps')
        os.makedirs(caps)
        for n in ('a', 'b'):
            with open(os.path.join(caps, f'{n}.json'), 'w') as f:
                json.dump(CASE, f)            # both files carry id 'synthetic'
        lib = self.build(False)
        results = verify.run_cases(self.boot, caps, '0x00400000', 'replacement', lib)
        res = lambda side: {'schema': 1, 'address': '0x00400000', 'cases': results[side]}
        out = verify.compare(res('original'), res('replacement'))
        self.assertEqual((out['cases'], out['failed']), (2, 0))

    def test_capture_provenance_is_optional_but_bound_when_present(self):
        caps = os.path.join(self.d, 'caps')
        os.makedirs(caps)
        path = os.path.join(caps, 'capture.json')
        captured = dict(CASE, capture={'function': 'synthetic_function', 'run_id': 'test-run_1'})
        with open(path, 'w') as f:
            json.dump(captured, f)
        lib = self.build(False)
        results = verify.run_cases(self.boot, caps, '0x00400000', 'replacement', lib,
                                   function='synthetic_function')
        self.assertEqual(len(results['original']), 1)
        with self.assertRaisesRegex(verify.BadInput, 'records .+, not'):
            verify.run_cases(self.boot, caps, '0x00400000', 'replacement', lib,
                             function='different_function')
        captured['capture'] = {'function': 'synthetic_function', 'run_id': '../escape'}
        with open(path, 'w') as f:
            json.dump(captured, f)
        with self.assertRaisesRegex(verify.BadInput, 'invalid capture run_id'):
            verify.run_cases(self.boot, caps, '0x00400000', 'replacement', lib,
                             function='synthetic_function')

    def test_legacy_capture_without_provenance_replays(self):
        caps = os.path.join(self.d, 'caps')
        os.makedirs(caps)
        with open(os.path.join(caps, 'legacy.json'), 'w') as f:
            json.dump(CASE, f)
        results = verify.run_cases(self.boot, caps, '0x00400000', 'replacement', self.build(False),
                                   function='synthetic_function')
        self.assertEqual(len(results['original']), 1)

    def test_direct_harness_rejects_wrong_case_address(self):
        wrong = dict(CASE, address='0x00400800')
        with open(self.case, 'w') as f:
            json.dump(wrong, f)
        cmd = [sys.executable, os.path.join(TOOLS, 'harness.py'), '--elf', self.boot,
               '--case', self.case, '--address', '0x00400000']
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        self.assertEqual(r.returncode, 3)
        self.assertIn('case is for 0x00400800', r.stderr)

    def test_stub_replaces_a_game_function(self):
        # Stub f itself at 0x400000 and call it through... the harness calls the stub directly.
        case = dict(CASE, stubs=[{'address': '0x00400000', 'ret': 5}], returns='i64', imports=[])
        with open(self.case, 'w') as f:
            json.dump(case, f)
        r = self.side()
        self.assertEqual(r['ret'], {'rax': '0x5'})
        self.assertEqual(r['calls'], [{'call': '0x00400000', 'args': []}])
        self.assertEqual(r['writes'], [])

    def test_series_entry_writes_one_value_per_call(self):
        # The function reads the first 8 bytes the import writes: the timeval's seconds.
        case = dict(CASE, imports=[{'name': 'testImport', 'argc': 2, 'ret': 0,
                                    'series': {'arg': 1, 'format': 'timeval_us',
                                               'values': [0x1234 * 1_000_000 + 7]}}])
        with open(self.case, 'w') as f:
            json.dump(case, f)
        r = self.side()
        self.assertIn({'addr': 'buf:out+0x0', 'bytes': '3413'}, r['writes'])
        self.assertNotIn('errors', r)

    def test_stack_arguments(self):
        case = dict(CASE, address='0x00400800', args={}, stack=['0x1', '0x20', '0x300'], returns='i64', imports=[])
        with open(self.case, 'w') as f:
            json.dump(case, f)
        cmd = [sys.executable, os.path.join(TOOLS, 'harness.py'), '--elf', self.boot, '--case', self.case,
               '--address', '0x00400800']
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        self.assertEqual(r.returncode, 0, r.stderr)
        out = json.loads(r.stdout)
        self.assertEqual(out['ret'], {'rax': '0x321'})
        self.assertEqual(out['preserved']['rsp'], 'kept')

    def test_unscripted_import_is_reported(self):
        case = dict(CASE, imports=[])
        with open(self.case, 'w') as f:
            json.dump(case, f)
        self.assertIn('errors', self.side())


if __name__ == '__main__':
    unittest.main()
