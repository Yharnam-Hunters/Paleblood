# SPDX-License-Identifier: GPL-2.0-or-later
"""game/render/tools/shader_survey.py on made-up containers (no game data): BND4 entries, shader files
and their registers, embedded shader files, and the reasons' grouping."""
import importlib.util
import os
import struct
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPEC = importlib.util.spec_from_file_location('shader_survey', os.path.join(ROOT, 'game', 'render', 'tools', 'shader_survey.py'))
survey = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(survey)

S_ENDPGM = struct.pack('<I', 0xbf810000)


def shader_file(stage: int, registers: list, code: bytes, shader_hash: int = 0x1234) -> bytes:
    """A shader file as the game's are laid out: Shdr header, register block, code, OrbShdr block."""
    block = struct.pack('<%dI' % len(registers), *registers)
    header = b'Shdr' + struct.pack('<HHBB', 7, 1, stage, len(registers)) + bytes(6)
    info = b'OrbShdr' + bytes([7]) + struct.pack('<I', len(code) << 8) + bytes(4) + struct.pack('<Q', shader_hash)
    return header + block + code + info


def bnd4(files: list) -> bytes:
    """A BND4 archive of (name, data), byte names, 0x24-byte entries."""
    entry_size, table = 0x24, 0x40
    names_at = table + entry_size * len(files)
    names = b''.join(name.encode() + b'\0' for name, _ in files)
    data_at = names_at + len(names)
    head = bytearray(0x40)
    head[0:4] = b'BND4'
    struct.pack_into('<I', head, 0xc, len(files))
    struct.pack_into('<Q', head, 0x10, table)
    struct.pack_into('<Q', head, 0x20, entry_size)
    entries, blobs, name_offset, offset = b'', b'', names_at, data_at
    for name, data in files:
        entry = bytearray(entry_size)
        struct.pack_into('<Q', entry, 8, len(data))
        struct.pack_into('<I', entry, 0x18, offset)
        struct.pack_into('<I', entry, entry_size - 4, name_offset)
        entries += bytes(entry)
        blobs += data
        name_offset += len(name) + 1
        offset += len(data)
    return bytes(head) + entries + names + blobs


class ShaderSurveyTest(unittest.TestCase):
    def test_entries_and_registers(self):
        # a pixel shader: lo, hi, rsrc1, rsrc2 (8 user SGPRs), z format 0, colour format, input enable/address
        pixel = shader_file(2, [0, 0, 0, 0xffffffff, 0, 8 << 1, 0, 4, 0x1002, 0x1002, 1, 0, 0, 0xf], S_ENDPGM)
        archive = bnd4([('a\\one.ppo', pixel), ('b.vpo', shader_file(1, [0, 0, 0, 0, 0, 4 << 1, 0, 4, 0], S_ENDPGM))])
        entries = list(survey.bnd4_entries(archive))
        self.assertEqual([name for name, _ in entries], ['a\\one.ppo', 'b.vpo'])
        stage, registers, code = survey.parse_shader(entries[0][1])
        self.assertEqual((stage, code), (2, S_ENDPGM))
        settings, refusals = survey.survey_arguments('pixel', registers)
        self.assertEqual(settings, ['user=8', 'inputs=0x1002'])
        self.assertEqual(refusals, [])  # perspective centre and front face: both drawn
        self.assertEqual(survey.shader_hash(entries[0][1]), 0x1234)

    def test_refusals(self):
        # compute with TG_SIZE_EN (RSRC2 bit 10); a pixel shader with the linear barycentrics (bit 4)
        _, refusals = survey.survey_arguments('compute', [0, 0, 0, 1 << 10, 64, 1, 1])
        self.assertTrue(refusals and 'TG_SIZE_EN' in refusals[0])
        _, refusals = survey.survey_arguments('pixel', [0, 0, 0, 0, 0, 0, 0x12, 0x12, 0, 0, 0, 0])
        self.assertTrue(refusals and 'pixel-shader inputs' in refusals[0])

    def test_embedded(self):
        program = shader_file(4, [0, 0, 0, 0, 0, 0, 64, 1, 1], S_ENDPGM, 0x99)
        with tempfile.NamedTemporaryFile(suffix='.elf') as elf:
            elf.write(b'\x7fELF' + bytes(60) + program + bytes(16) + program)
            elf.flush()
            found = list(survey.embedded_shaders(elf.name))
        self.assertEqual(len(found), 2)
        self.assertEqual(survey.parse_shader(found[0][2])[0], 4)
        self.assertEqual(survey.shader_hash(found[1][2]), 0x99)

    def test_reason_key(self):
        self.assertEqual(survey.reason_key('image_sample: the descriptor in s[16:23] at 0x40'),
                         'image_sample: the descriptor in s[N:N] at N')


if __name__ == '__main__':
    unittest.main()
