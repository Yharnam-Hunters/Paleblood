#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate the GCN (GFX7, the PS4 GPU's generation) opcode tables for the shader decoder.

The instruction names and opcode numbers come from two public sources that have to agree:
AMD's Sea Islands instruction set manual (the encodings, and the GFX7-only instructions listed
below) and LLVM's AMDGPU assembler (llvm-mc). LLVM disassembles only GFX8 and later, so:

1. Every GFX8 encoding's opcode numbers are disassembled with llvm-mc -mcpu=tonga, which lists
   the instruction names of each encoding.
2. Each name, and each GFX7-only name, is assembled with llvm-mc -mcpu=bonaire (GFX7). The GFX7
   encoding the assembler produces gives the instruction's GFX7 encoding and opcode.

A name that does not assemble for GFX7 does not exist there and is left out. Two names on one
encoding and opcode are an error. No game data is read.

Needs llvm-mc with the AMDGPU target; run it by hand and commit the output:
    python3 tools/gcn_tables.py > runtime/renderer/gcn_opcodes.inc
"""
import re
import subprocess
import sys

# GFX7 encodings: (name, test on the first dword, opcode field shift, mask), most specific first.
GFX7_ENCODINGS = (
    ('SOP1', lambda w: w >> 23 == 0x17d, 8, 0xff),
    ('SOPC', lambda w: w >> 23 == 0x17e, 16, 0x7f),
    ('SOPP', lambda w: w >> 23 == 0x17f, 16, 0x7f),
    ('SOPK', lambda w: w >> 28 == 0xb, 23, 0x1f),
    ('SOP2', lambda w: w >> 30 == 0x2, 23, 0x7f),
    ('SMRD', lambda w: w >> 27 == 0x18, 22, 0x1f),
    ('VOP1', lambda w: w >> 25 == 0x3f, 9, 0xff),
    ('VOPC', lambda w: w >> 25 == 0x3e, 17, 0xff),
    ('VOP2', lambda w: w >> 31 == 0, 25, 0x3f),
    ('VOP3', lambda w: w >> 26 == 0x34, 17, 0x1ff),
    ('VINTRP', lambda w: w >> 26 == 0x32, 16, 0x3),
    ('DS', lambda w: w >> 26 == 0x36, 18, 0xff),
    ('FLAT', lambda w: w >> 26 == 0x37, 18, 0x7f),
    ('MUBUF', lambda w: w >> 26 == 0x38, 18, 0x7f),
    ('MTBUF', lambda w: w >> 26 == 0x3a, 16, 0x7),
    ('MIMG', lambda w: w >> 26 == 0x3c, 18, 0x7f),
    ('EXP', lambda w: w >> 26 == 0x3e, 0, 0),
)

# The decoder's encoding order (gcn_encoding in runtime/gcn.h): the table is emitted sorted by it.
DECODER_ORDER = [(e,) for e in ('SOP2', 'SOPK', 'SOP1', 'SOPC', 'SOPP', 'SMRD', 'VOP2', 'VOP1', 'VOPC', 'VOP3',
                                'VINTRP', 'DS', 'FLAT', 'MUBUF', 'MTBUF', 'MIMG', 'EXP')]

# GFX8 encodings to enumerate names from: (first dword for opcode op, second dwords to try, count).
# The disassembler rejects an instruction whose unused operand fields are not zero, so memory
# instructions are tried with several operand layouts.
GFX8_ENUMERATION = (
    (lambda op: 0x80000000 | op << 23 | 1 << 16 | 3 << 8 | 2, (), 128),           # SOP2
    (lambda op: 0xb0000000 | op << 23 | 1 << 16 | 0x10, (), 32),                  # SOPK
    (lambda op: 0xbe800000 | 1 << 16 | op << 8 | 2, (), 256),                     # SOP1
    (lambda op: 0xbf000000 | op << 16 | 3 << 8 | 2, (), 128),                     # SOPC
    (lambda op: 0xbf800000 | op << 16 | 1, (), 128),                              # SOPP
    (lambda op: 0xc0000000 | op << 18 | 1 << 6 | 2, (0x10,), 256),                  # SMEM
    (lambda op: op << 25 | 1 << 17 | 3 << 9 | 0x102, (), 64),                     # VOP2
    (lambda op: 0x7e000000 | 1 << 17 | op << 9 | 0x102, (), 256),                 # VOP1
    (lambda op: 0x7c000000 | op << 17 | 3 << 9 | 0x102, (), 256),                 # VOPC
    (lambda op: 0xd0000000 | op << 16 | 1, (0x102 | 0x103 << 9 | 0x104 << 18, 0x102 | 0x103 << 9), 1024),  # VOP3
    (lambda op: 0xd4000000 | 1 << 18 | op << 16 | 2, (), 4),                      # VINTRP
    (lambda op: 0xd8000000 | op << 17, (0x01000002, 0x00000201, 0x01000302, 0x00030201, 0x01030201), 256),  # DS
    (lambda op: 0xdc000000 | op << 18, (0x01000002, 0x00000402, 0x01000402), 128),  # FLAT
    (lambda op: 0xe0000000 | op << 18 | 1 << 12, (0x01010102, 0x80010102), 128),     # MUBUF
    (lambda op: 0xe8000000 | op << 15 | 1 << 12, (0x01010102, 0x80010102), 16),      # MTBUF
    (lambda op: 0xf0000000 | op << 18 | 0xf << 8, (0x00820102, 0x00040201, 0x00020102), 128),  # MIMG
)

# GFX7 instructions the GFX8 enumeration cannot list: those GFX8 dropped or renamed (from the Sea
# Islands manual), and those that always carry a literal constant, which a one-dword disassembly
# cannot show.
GFX7_ONLY = '''
v_madmk_f32 v_madak_f32 v_mul_lo_i32 v_readlane_b32 v_writelane_b32 v_readfirstlane_b32 s_barrier
ds_append ds_consume
s_cbranch_join s_cbranch_i_fork s_cbranch_g_fork s_dcache_inv_vol s_memtime s_dcache_inv
s_load_dword s_load_dwordx2 s_load_dwordx4 s_load_dwordx8 s_load_dwordx16
s_buffer_load_dword s_buffer_load_dwordx2 s_buffer_load_dwordx4 s_buffer_load_dwordx8
s_buffer_load_dwordx16 v_mac_legacy_f32 v_min_legacy_f32 v_max_legacy_f32 v_add_i32 v_sub_i32
v_subrev_i32 v_addc_u32 v_subb_u32 v_subbrev_u32 v_lshl_b32 v_ashr_i32 v_lshr_b32 v_bfm_b32
v_bcnt_u32_b32 v_mbcnt_lo_u32_b32 v_mbcnt_hi_u32_b32 v_cvt_pkaccum_u8_f32 v_cvt_pknorm_i16_f32
v_cvt_pknorm_u16_f32 v_cvt_pkrtz_f16_f32 v_cvt_pk_u16_u32 v_cvt_pk_i16_i32 v_ldexp_f32
v_lshl_b64 v_lshr_b64 v_ashr_i64 v_mullit_f32 v_log_clamp_f32 v_rcp_clamp_f32 v_rcp_legacy_f32
v_rsq_clamp_f32 v_rsq_legacy_f32 v_rcp_clamp_f64 v_rsq_clamp_f64 v_mul_legacy_f32
v_mad_legacy_f32 v_log_legacy_f32 v_exp_legacy_f32 v_trig_preop_f64 v_qsad_pk_u16_u8
v_mqsad_pk_u16_u8 v_mqsad_u32_u8 v_mad_u64_u32 v_mad_i64_i32 v_sad_u32 v_sad_hi_u8 v_sad_u16
v_sad_u8 v_cvt_f32_ubyte0 v_cvt_f32_ubyte1 v_cvt_f32_ubyte2 v_cvt_f32_ubyte3 v_movreld_b32
v_movrels_b32 v_movrelsd_b32 v_cvt_flr_i32_f32 v_cvt_rpi_i32_f32 v_frexp_mant_f32
v_frexp_exp_i32_f32 v_ffbh_u32 v_ffbl_b32 v_ffbh_i32
'''.split()
GFX7_ONLY += ['image_gather4' + suffix for suffix in (
    '', '_cl', '_l', '_b', '_b_cl', '_lz', '_c', '_c_cl', '_c_l', '_c_b', '_c_b_cl', '_c_lz', '_o', '_cl_o', '_l_o',
    '_b_o', '_b_cl_o', '_lz_o', '_c_o', '_c_cl_o', '_c_l_o', '_c_b_o', '_c_b_cl_o', '_c_lz_o')]
GFX7_ONLY += [f'v_cmps_{c}_{t}' for c in 'f lt eq le gt lg ge o u nge lg nlg ngt nle neq nlt tru'.split()
              for t in ('f32', 'f64')]
GFX7_ONLY += [f'v_cmpsx_{c}_{t}' for c in 'f lt eq le gt lg ge o u nge nlg ngt nle neq nlt tru'.split()
              for t in ('f32', 'f64')]

# Operand shapes tried for assembling a name, until one assembles.
OPERANDS = (
    '', 'v1, v2, v3', 'v1, v2', 'vcc, v2, v3', 'v[1:2], v[2:3], v[4:5]', 'v[1:2], v[2:3]',
    'v1, v[2:3]', 'v[1:2], v3', 'vcc, v[2:3], v[4:5]', 'v1, v2, v3, v4', 'v[1:2], v[2:3], v[4:5], v[6:7]',
    'v1, vcc, v2, v3', 'v1, vcc, v2, v3, vcc', 'v1, s[2:3], v2, v3, s[4:5]', 'v[1:2], s[2:3], v[2:3], v[4:5], v[6:7]',
    'v[1:2], s[4:5], v2, v3, v[6:7]', 'v[1:2], v2, v[4:5]', 'v1, v2, v[4:5]', 'v1, v[2:3], v4',
    'v1, v2, 0x40000000', 'v1, 0x40000000, v2, v3', 'v1, v2, 0x40000000, v3', 'v1, v2, v3, 0x40000000',
    's1, s2, s3', 's[2:3], s[4:5], s[6:7]', 's[2:3], s[4:5], s6', 's1, s[2:3], s4', 's1, s2', 's[2:3], s[4:5]',
    's1, s[2:3]', 's[2:3], s1', 's[2:3], s4', 's1, 0x10', '0x10', '1', 's[4:5]', 's1',
    's1, s[2:3], 0x1', 's[2:3], s[4:5], 0x1', 's[4:7], s[2:3], 0x1', 's[4:11], s[2:3], 0x1',
    's[4:19], s[2:3], 0x1', 's1, s[4:7], 0x1', 's[2:3], s[4:7], 0x1', 's[8:11], s[4:7], 0x1',
    's[8:15], s[4:7], 0x1', 's[8:23], s[4:7], 0x1',
    'v1, v2, s[4:7], s1 offen', 'v[1:2], v2, s[4:7], s1 offen', 'v[1:3], v2, s[4:7], s1 offen',
    'v[1:4], v2, s[4:7], s1 offen', 'v1, v2, s[4:7], s1 offen glc', 'v[1:2], v2, s[4:7], s1 offen glc',
    'v1, v2, s[4:7], dfmt:1, nfmt:0, s1 offen',
    'v1, v2', 'v1, v2, v3', 'v[1:2], v2', 'v1, v2, v[3:4]', 'v[1:2], v2, v[3:4]', 'v2, v3', 'v2, v[3:4]',
    'v[1:4], v2, s[8:15], s[16:19] dmask:0xf', 'v[1:4], v[2:3], s[8:15], s[16:19] dmask:0xf',
    'v[1:4], v[2:4], s[8:15], s[16:19] dmask:0xf', 'v[1:4], v[2:5], s[8:15], s[16:19] dmask:0xf',
    'v[1:4], v2, s[8:15] dmask:0xf', 'v[1:4], v[2:3], s[8:15] dmask:0xf', 'v[1:4], v[2:5], s[8:15] dmask:0xf',
    'v1, v2, s[8:15] dmask:0x1', 'v1, v[2:3], s[8:15], s[16:19] dmask:0x1',
    'v1, v2, attr0.x', 'v1, p10, attr0.x', 's1, v2, s3', 'v1, s2, s3', 'v1, s2, 3', 's1, v2', 'v1', 'v[1:2], v[2:3], v4',
    'v[1:4], v2, s[8:15], s[16:19] dmask:0x1', 'v[1:4], v[2:3], s[8:15], s[16:19] dmask:0x1',
    'v[1:4], v[2:4], s[8:15], s[16:19] dmask:0x1', 'v[1:4], v[2:5], s[8:15], s[16:19] dmask:0x1',
    'v[1:4], v[2:6], s[8:15], s[16:19] dmask:0x1', 'v[1:4], v[2:9], s[8:15], s[16:19] dmask:0x1',
    'v1, v[2:3]', 'v[1:2], v[2:3]', 'v[1:4], v[2:3]', 'v[2:3], v1', 'v[2:3], v[4:5]', 'v[2:3], v[4:7]',
)


def llvm(cpu: str, text: str, disassemble: bool) -> str:
    args = ['llvm-mc', '-triple=amdgcn', f'-mcpu={cpu}'] + (['--disassemble'] if disassemble else ['--show-encoding'])
    return subprocess.run(args, input=text, capture_output=True, text=True).stdout


def gfx8_names() -> set[str]:
    names = set()
    for first, seconds, count in GFX8_ENUMERATION:
        for op in range(count):
            for second in seconds or (None,):
                words = [first(op)] + ([second] if second is not None else [])
                text = ' '.join(f'0x{b:02x}' for w in words for b in w.to_bytes(4, 'little')) + '\n'
                lines = [ln.strip() for ln in llvm('tonga', text, True).splitlines()
                         if ln.strip() and not ln.strip().startswith(('.', ';'))]
                # A stray second dword can decode on its own; every name is checked against GFX7 below.
                names.update(re.sub(r'_(e32|e64|sdwa|dpp)$', '', ln.split()[0]) for ln in lines)
    return names


def gfx7_encoding(name: str):
    """The (encoding, opcode) GFX7 gives name, or None when no operand shape assembles."""
    for operands in OPERANDS:
        out = llvm('bonaire', f'{name} {operands}\n', False)
        m = re.search(r'encoding: \[([^\]]+)\]', out)
        if not m:
            continue
        data = [int(b, 16) for b in m.group(1).split(',')]
        word = int.from_bytes(bytes(data[:4]), 'little')
        for enc, test, shift, mask in GFX7_ENCODINGS:
            if test(word):
                return enc, (word >> shift) & mask
    return None


def main() -> int:
    names = sorted(gfx8_names() | set(GFX7_ONLY))
    table, missing = {}, []
    for name in names:
        found = gfx7_encoding(name)
        if not found:
            missing.append(name)
            continue
        if found in table and table[found] != name:
            print(f'gcn_tables: {found} is both {table[found]} and {name}', file=sys.stderr)
            return 1
        table[found] = name
    print('/* Generated by tools/gcn_tables.py from AMD\'s Sea Islands manual and LLVM\'s AMDGPU')
    print('   assembler (GFX7). Do not edit. */')
    order = [enc for enc, *_ in DECODER_ORDER]
    for (enc, op), name in sorted(table.items(), key=lambda item: (order.index(item[0][0]), item[0][1])):
        print(f'GCN_OPCODE({enc}, 0x{op:03x}, "{name}")')
    print(f'gcn_tables: {len(table)} opcodes; {len(missing)} GFX8 names have no GFX7 form', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
