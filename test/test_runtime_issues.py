import csv
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'tools'))
import runtime_issues
import boot


class RuntimeIssueStatusTests(unittest.TestCase):
    def test_repository_entries_mark_the_issue_bodies(self):
        entries = runtime_issues.read_runtime_entries(runtime_issues.ROOT)

        # getpagesize is registered for two libraries (runtime/syslib/kernel.c); the disc map's
        # unnamed import by its NID (runtime/syslib/disc_map.c).
        self.assertIn(('libSceNetCtl', 'sceNetCtlGetNatInfo'), entries)
        self.assertIn(('libkernel', 'getpagesize'), entries)
        self.assertIn(('libScePosix', 'getpagesize'), entries)
        self.assertIn(('libSceDiscMap', 'ioKMruft1ek#'), entries)

        rows = runtime_issues.read_imports(runtime_issues.ROOT)
        kernel = next(group for group in runtime_issues.GROUPS if group[0] == 'kernel')
        body = runtime_issues.issue(kernel, rows, entries)['body']
        self.assertIn('- [x] `getpagesize`', body)
        files = next(group for group in runtime_issues.GROUPS if group[0] == 'files')
        self.assertIn('- [x] `ioKMruft1ek#A#B`', runtime_issues.issue(files, rows, entries)['body'])
        network = runtime_issues.issue(runtime_issues.GROUPS[-1], rows, entries)['body']
        self.assertIn('- [x] `sceNetCtlGetNatInfo`', network)

    def test_nid_entries_match_only_their_unnamed_import(self):
        with tempfile.TemporaryDirectory() as root:
            directory = os.path.join(root, 'runtime', 'syslib')
            os.makedirs(directory)
            with open(os.path.join(directory, 'disc_map.c'), 'w', encoding='utf-8') as source:
                source.write('''
                    // RT_SYSLIB_NID("libSceDiscMap", "commentedNI", fn);
                    RT_SYSLIB_NID ( "libSceDiscMap", "ioKMruft1ek", request );
                    RT_SYSLIB_NID("libSceDiscMap", "tooShort", other);
                ''')
            entries = runtime_issues.read_runtime_entries(root)

        self.assertEqual({('libSceDiscMap', 'ioKMruft1ek#')}, entries)
        row = lambda library, symbol: {'library': library, 'symbol': symbol}
        self.assertTrue(runtime_issues.registered(row('libSceDiscMap', 'ioKMruft1ek#A#B'), entries))
        self.assertFalse(runtime_issues.registered(row('libSceDiscMap', 'ioKMruft1ek'), entries))
        self.assertFalse(runtime_issues.registered(row('libSceOther', 'ioKMruft1ek#A#B'), entries))
        self.assertFalse(runtime_issues.registered(row('libSceDiscMap', 'ioKMruft1el#A#B'), entries))

    def test_runtime_unresolved_provider_does_not_count_as_implemented(self):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(os.path.join(root, 'symbols'))
            with open(os.path.join(root, 'symbols', 'imports.csv'), 'w', encoding='utf-8') as imports:
                imports.write('importer,library,symbol,kind,provider\n')
                imports.write('eboot,libSceNetCtl,sceNetCtlGetInfo,function,runtime-unresolved\n')

            rows = runtime_issues.read_imports(root)

        self.assertEqual('no', rows[0]['implemented'])

    def test_boot_listing_does_not_track_unresolved_runtime_as_a_module(self):
        with tempfile.TemporaryDirectory() as root:
            os.makedirs(os.path.join(root, 'symbols'))
            boot.write_listing(
                'eboot\tlibSceNetCtl\tsceNetCtlGetInfo\tfunction\truntime-unresolved\t0x0\t0\n', root)

            with open(os.path.join(root, 'symbols', 'module_functions.csv'), newline='', encoding='utf-8') as listing:
                self.assertEqual(['module', 'offset', 'size', 'symbol', 'status'], next(csv.reader(listing)))

    def test_registered_symbols_are_read_from_runtime_sources(self):
        with tempfile.TemporaryDirectory() as root:
            directory = os.path.join(root, 'runtime', 'syslib')
            os.makedirs(directory)
            with open(os.path.join(directory, 'network.c'), 'w', encoding='utf-8') as source:
                source.write('''
                    // RT_SYSLIB("libSceNet", commented_line, fn);
                    /* RT_SYSLIB("libSceNet", commented_block, fn); */
                    RT_SYSLIB ( "libSceNet", sceNetHtonl, net_htonl );
                    RT_SYSLIB("libSceNet", sceNetHtons, net_htons);
                    RT_SYSLIB("libSceNet", sceNetNtohl, net_ntohl);
                    RT_SYSLIB("libSceNet", sceNetNtohs, net_ntohs);
                    RT_SYSLIB_UNRESOLVED("libSceNetCtl", sceNetCtlGetInfo, netctl_get_info);
                    RT_SYSLIB_UNRESOLVED("libSceNetCtl", sceNetCtlGetNatInfo, netctl_get_nat_info);
                ''')

            self.assertEqual({
                ('libSceNet', 'sceNetHtonl'),
                ('libSceNet', 'sceNetHtons'),
                ('libSceNet', 'sceNetNtohl'),
                ('libSceNet', 'sceNetNtohs'),
            }, runtime_issues.read_runtime_entries(root))

            self.assertNotIn(('libSceNetCtl', 'sceNetCtlGetInfo'), runtime_issues.read_runtime_entries(root))
            self.assertNotIn(('libSceNetCtl', 'sceNetCtlGetNatInfo'), runtime_issues.read_runtime_entries(root))

    def test_issue_counts_and_marks_registered_symbols(self):
        symbols = ('sceNetHtonl', 'sceNetHtons', 'sceNetNtohl', 'sceNetNtohs',
                   'sceNetCtlGetInfo', 'sceNetCtlGetNatInfo', 'sceNetSocket')
        rows = [{'library': 'libSceNet', 'symbol': symbol, 'implemented': 'no',
                 'kind': 'function', 'importers': {'eboot'}} for symbol in symbols]
        entries = {('libSceNet', symbol) for symbol in symbols[:4]}

        body = runtime_issues.issue(runtime_issues.GROUPS[-1], rows, entries)['body']

        self.assertIn('4 of 7 imported functions implemented', body)
        self.assertIn('### libSceNet (4 of 7)', body)
        for symbol in symbols[:4]:
            self.assertIn(f'- [x] `{symbol}`', body)
        for symbol in symbols[4:]:
            self.assertIn(f'- [ ] `{symbol}`', body)


if __name__ == '__main__':
    unittest.main()
