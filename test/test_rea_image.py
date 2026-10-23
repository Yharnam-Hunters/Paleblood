# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic ELF analysis-view tests; these do not recover or execute any game code."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import check_target
import rea_image
import ps4elf


def sections(data):
    offset = struct.unpack_from('<Q', data, 40)[0]
    count, names_index = struct.unpack_from('<HH', data, 60)
    rows = [struct.unpack_from('<IIQQQQIIQQ', data, offset + 64 * index) for index in range(count)]
    names_row = rows[names_index]
    names = data[names_row[4]:names_row[4] + names_row[5]]
    result = {}
    for index, row in enumerate(rows[1:], 1):
        name = names[row[0]:].split(b'\0', 1)[0].decode()
        result[name] = {'index': index, 'type': row[1], 'flags': row[2], 'address': row[3],
                        'offset': row[4], 'size': row[5], 'link': row[6], 'info': row[7],
                        'alignment': row[8], 'entry_size': row[9]}
    return result


def symbols(data, rows):
    symtab, strtab = rows['.symtab'], rows['.strtab']
    strings = data[strtab['offset']:strtab['offset'] + strtab['size']]
    result = []
    for offset in range(symtab['offset'], symtab['offset'] + symtab['size'], symtab['entry_size']):
        name, info, other, section, value, size = struct.unpack_from('<IBBHQQ', data, offset)
        end = strings.index(b'\0', name)
        result.append({'name': strings[name:end].decode(), 'info': info,
                       'other': other, 'section': section, 'value': value, 'size': size})
    return result


class ReaImage(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name).resolve()
        self.repo = self.directory / 'repo'
        (self.repo / 'symbols').mkdir(parents=True)
        self.source = self.directory / 'source.elf'
        self.output = self.directory / 'analysis.elf'
        self.provenance = self.directory / 'analysis.json'
        self.source.write_bytes(ps4elf.build(
            segments=[(0, b'\xc3' * 0x40 + b'synthetic readonly data' * 2, 0x100, 5),
                      (0x1000, b'synthetic writable data', 0x40, 6)],
            symbols=[], relocations=[]))
        self.catalog = self.repo / 'symbols/ghidra_functions.csv'
        self.names = self.repo / 'symbols/functions.csv'
        self.catalog.write_text('address,size\n0x00400000,4\n0x00400010,8\n0x00400030,4\n')
        self.names.write_text('address,size,name,system,status,notes\n'
                              '0x00400010,8,example_named_function,example,original,synthetic\n')
        self.pin()

    def tearDown(self):
        self.temporary.cleanup()

    def pin(self):
        self.pin_file()
        self.repo.joinpath('target.image.sha256').write_text(
            check_target.loaded_image_sha256(str(self.source)) + '  eboot.elf\n')

    def pin_file(self):
        self.repo.joinpath('target.sha256').write_text(
            hashlib.sha256(self.source.read_bytes()).hexdigest() + '  eboot.elf\n')

    def build(self, output=None, provenance=None):
        return rea_image.create_analysis_image(self.source, output or self.output,
                                              provenance or self.provenance, repo_root=self.repo)

    def assert_no_outputs(self):
        self.assertFalse(self.output.exists())
        self.assertFalse(self.provenance.exists())

    def test_only_section_header_fields_change_in_the_original_file_extent(self):
        original = self.source.read_bytes()
        report = self.build()
        derived = self.output.read_bytes()
        self.assertEqual(self.source.read_bytes(), original)
        changed = {index for index, (left, right) in enumerate(zip(original, derived)) if left != right}
        self.assertTrue(changed)
        self.assertTrue(changed <= set(range(40, 48)) | set(range(58, 64)))
        self.assertEqual(derived[:16], original[:16])  # OSABI and identification unchanged
        self.assertEqual(derived[16:40], original[16:40])  # e_type, machine, entry, PHDR offset
        self.assertTrue(all(report['preservation'].values()))
        self.assertEqual(check_target.loaded_image_sha256(str(self.source)),
                         check_target.loaded_image_sha256(str(self.output)))
        loads, offset, length = rea_image.parse_loads(original)
        self.assertEqual(derived[offset:offset + length], original[offset:offset + length])
        for load in loads:
            start, length = load['file_offset'], load['file_size']
            self.assertEqual(derived[start:start + length], original[start:start + length])

    def test_symbols_are_catalog_seeds_with_zero_size_and_base_subtracted_values(self):
        self.build()
        data = self.output.read_bytes()
        rows = sections(data)
        seeds = symbols(data, rows)
        self.assertEqual(seeds[0], {'name': '', 'info': 0, 'other': 0, 'section': 0, 'value': 0, 'size': 0})
        self.assertEqual([row['name'] for row in seeds[1:]],
                         ['pb_catalog_00400000', 'example_named_function', 'pb_catalog_00400030'])
        self.assertEqual([row['value'] for row in seeds[1:]], [0, 0x10, 0x30])
        self.assertEqual({row['size'] for row in seeds}, {0})
        self.assertEqual({row['info'] for row in seeds[1:]}, {0x12})
        self.assertEqual({row['section'] for row in seeds[1:]}, {rows['.text']['index']})
        self.assertEqual(rows['.symtab']['link'], rows['.strtab']['index'])
        self.assertEqual(rows['.symtab']['info'], 1)
        self.assertEqual(rows['.symtab']['alignment'], 8)
        self.assertEqual(rows['.symtab']['offset'] % 8, 0)
        self.assertEqual(rows['.symtab']['entry_size'], 24)

    def test_text_extent_is_an_explicit_catalog_hint_with_rodata_remainder(self):
        report = self.build()
        rows = sections(self.output.read_bytes())
        self.assertEqual(rows['.text']['address'], 0)
        self.assertEqual(rows['.text']['size'], 0x34)
        self.assertEqual(rows['.text']['flags'], 6)
        self.assertEqual(rows['.rodata']['address'], 0x34)
        self.assertEqual(rows['.rodata']['offset'], rows['.text']['offset'] + 0x34)
        self.assertEqual(rows['.rodata']['flags'], 2)
        self.assertEqual(rows['.data']['address'], 0x1000)
        self.assertEqual(rows['.data']['flags'], 3)
        self.assertEqual(report['section_extent_policy'], 'catalog_max_start_plus_size_analysis_hint')
        self.assertTrue(any('bounds remain unverified' in text for text in report['limitations']))

    def test_provenance_binds_all_inputs_and_preservation_without_claiming_verification(self):
        report = self.build()
        self.assertEqual(json.loads(self.provenance.read_text()), report)
        self.assertEqual(report['schema_version'], 1)
        self.assertEqual(report['kind'], 'paleblood_rea_analysis_image')
        for key, path in (('original', self.source), ('derived', self.output),
                          ('catalog', self.catalog), ('names', self.names)):
            self.assertEqual(report[key]['sha256'], hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertEqual(report['original']['loaded_image_sha256'],
                         report['derived']['loaded_image_sha256'])
        self.assertEqual(report['catalog']['function_symbols'], 3)
        self.assertEqual(report['catalog']['named_symbols'], 1)
        self.assertEqual(report['catalog']['generated_symbols'], 2)
        self.assertEqual(report['address_transform']['subtract'], 0x400000)
        self.assertTrue(any('not independent function discovery' in text for text in report['limitations']))
        self.assertTrue(any('no runtime behavior' in text for text in report['limitations']))

    def test_requires_exact_original_digest(self):
        self.source.write_bytes(self.source.read_bytes() + b'unrelated change')
        with self.assertRaisesRegex(ValueError, 'source SHA-256'):
            self.build()
        self.assert_no_outputs()

    def test_requires_exact_loaded_image_digest(self):
        self.repo.joinpath('target.image.sha256').write_text('0' * 64 + '  eboot.elf\n')
        with self.assertRaisesRegex(ValueError, 'loaded-image SHA-256'):
            self.build()
        self.assert_no_outputs()

    def test_refuses_preexisting_sections_instead_of_rewriting(self):
        data = bytearray(self.source.read_bytes())
        struct.pack_into('<Q', data, 40, len(data))
        self.source.write_bytes(data)
        self.pin_file()
        with self.assertRaisesRegex(ValueError, 'already has section metadata'):
            self.build()
        self.assert_no_outputs()

    def test_refuses_missing_or_ambiguous_target_pin(self):
        for text in ('a' * 64 + '  other.elf\n', ('a' * 64 + '  eboot.elf\n') * 2,
                     'not-a-hash  eboot.elf\n'):
            with self.subTest(text=text):
                self.repo.joinpath('target.sha256').write_text(text)
                with self.assertRaisesRegex(ValueError, 'SHA-256 pin'):
                    self.build()
                self.assert_no_outputs()

    def test_invalid_program_header_extent_is_rejected(self):
        data = bytearray(self.source.read_bytes())
        struct.pack_into('<Q', data, 32, len(data) - 1)
        self.source.write_bytes(data)
        self.pin_file()
        with self.assertRaisesRegex(ValueError, 'program-header table'):
            self.build()
        self.assert_no_outputs()

    def test_header_fields_cannot_also_be_loadable_bytes(self):
        data = bytearray(self.source.read_bytes())
        struct.pack_into('<Q', data, 64 + 8, 0)  # first PT_LOAD p_offset
        self.source.write_bytes(data)
        self.pin_file()
        with self.assertRaisesRegex(ValueError, 'overlap a file-backed load segment'):
            self.build()
        self.assert_no_outputs()

    def test_catalog_starts_must_be_in_file_backed_executable_memory(self):
        for address in ('0x003fffff', '0x004000f0', '0x00401000', '0x00404000'):
            with self.subTest(address=address):
                self.catalog.write_text(f'address,size\n{address},4\n')
                with self.assertRaises(ValueError):
                    self.build()
                self.assert_no_outputs()

    def test_duplicate_catalog_address_or_nonpositive_size_is_refused(self):
        for rows in ('0x00400000,4\n0x00400000,8\n', '0x00400000,0\n', '0x00400000,-1\n'):
            with self.subTest(rows=rows):
                self.catalog.write_text('address,size\n' + rows)
                with self.assertRaisesRegex(ValueError, 'duplicate or invalid'):
                    self.build()
                self.assert_no_outputs()

    def test_named_symbols_must_be_unique_and_catalog_bound(self):
        for rows in ('0x00400000,duplicate\n0x00400010,duplicate\n',
                     '0x00400000,first\n0x00400000,second\n',
                     '0x00409999,absent\n', '0x00400000,white space\n'):
            with self.subTest(rows=rows):
                self.names.write_text('address,name\n' + rows)
                with self.assertRaises(ValueError):
                    self.build()
                self.assert_no_outputs()

    def test_generated_names_do_not_conflict_with_analyst_names(self):
        self.names.write_text('address,name\n0x00400000,pb_catalog_00400010\n')
        self.build()
        data = self.output.read_bytes()
        names = [row['name'] for row in symbols(data, sections(data))]
        self.assertEqual(len(names), len(set(names)))
        self.assertIn('pb_catalog_00400010_seed', names)

    def test_no_output_or_provenance_may_be_in_repository_even_through_symlinks(self):
        link = self.directory / 'repo-link'
        link.symlink_to(self.repo, target_is_directory=True)
        for output, provenance in ((self.repo / 'bad.elf', self.provenance),
                                   (self.output, self.repo / 'bad.json'),
                                   (link / 'bad.elf', self.provenance)):
            with self.subTest(output=output, provenance=provenance):
                with self.assertRaisesRegex(ValueError, 'outside the repository'):
                    self.build(output, provenance)
                self.assert_no_outputs()
        self.assertFalse(self.repo.joinpath('bad.elf').exists())

    def test_exclusive_destinations_preserve_existing_files(self):
        for destination in (self.output, self.provenance):
            with self.subTest(destination=destination):
                destination.write_text('keep existing file')
                with self.assertRaises(FileExistsError):
                    self.build()
                self.assertEqual(destination.read_text(), 'keep existing file')
                destination.unlink()
                self.assert_no_outputs()

    def test_source_and_pair_paths_must_be_distinct(self):
        original = self.source.read_bytes()
        for output, provenance in ((self.source, self.provenance), (self.output, self.source),
                                   (self.output, self.output)):
            with self.subTest(output=output, provenance=provenance):
                with self.assertRaises(ValueError):
                    self.build(output, provenance)
                self.assert_no_outputs()
                self.assertEqual(self.source.read_bytes(), original)

    def test_failed_second_file_creation_removes_only_new_first_file(self):
        missing_parent = self.directory / 'missing/evidence.json'
        with self.assertRaises(FileNotFoundError):
            self.build(provenance=missing_parent)
        self.assert_no_outputs()
        self.assertFalse(missing_parent.exists())

    def test_multiple_executable_segments_map_symbols_to_their_own_text_sections(self):
        self.source.write_bytes(ps4elf.build(
            segments=[(0, b'\xc3' * 0x40, 0x40, 5), (0x1000, b'\xc3' * 0x40, 0x40, 5)],
            symbols=[], relocations=[]))
        self.catalog.write_text('address,size\n0x00400000,4\n0x00401010,8\n')
        self.names.write_text('address,name\n')
        self.pin()
        self.build()
        data = self.output.read_bytes()
        rows = sections(data)
        seeds = symbols(data, rows)[1:]
        self.assertEqual([row['value'] for row in seeds], [0, 0x1010])
        self.assertEqual([row['section'] for row in seeds], [rows['.text']['index'], rows['.text.1']['index']])
        self.assertEqual(rows['.text.1']['address'], 0x1000)
        self.assertEqual(rows['.rodata.1']['address'], 0x1018)


if __name__ == '__main__':
    unittest.main()
