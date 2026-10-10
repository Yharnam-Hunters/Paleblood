# SPDX-License-Identifier: GPL-2.0-or-later
import json
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import runtime_pr
import session_report


def runtime_body(**updates):
    report = {
        'schema': 1,
        'track': 'runtime',
        'calls': ['getpagesize'],
        'sources': ['https://man.freebsd.org/cgi/man.cgi?query=getpagesize'],
        'confirmed': ['returns the target page size in bytes'],
        'unresolved': [],
        'tests': ['CI: build and RT_SYSLIB test passed'],
        'clean_room': True,
        'no_emulator_code': True,
        'clean_room_basis': 'FreeBSD manual and independent implementation',
        'game_data': False,
        'function_verify': 'not-required',
        'maintainer_boot': 'pending-after-merge',
    }
    report.update(updates)
    return ('<!-- runtime-report:start -->\n'
            '<!-- runtime-report-json ' + json.dumps(report) + ' -->\n'
            '<!-- runtime-report:end -->\n')


def documentation_body(**updates):
    report = {
        'schema': 1,
        'track': 'documentation',
        'pages': ['docs/runtime-syscalls.md'],
        'summary': 'Clarifies the documented syscall behavior and target unknowns.',
        'implementation_changes': False,
    }
    report.update(updates)
    return ('<!-- documentation-report:start -->\n'
            '<!-- documentation-report-json ' + json.dumps(report) + ' -->\n'
            '<!-- documentation-report:end -->\n')


def decomp_body(replaced=None, verify=None):
    if replaced is None:
        replaced = [{'address': '0x00001000', 'name': 'frame_timing_update'}]
    if verify is None:
        verify = [{'function': 'frame_timing_update', 'cases': 3, 'passed': 3, 'failed': 0}]
    report = {
        'schema': 1,
        'base': 'origin/main',
        'branch': 'topic',
        'commits': 1,
        'functions': {'named': [], 'replaced': replaced, 'edge-verified': [], 'verified': []},
        'verify': verify,
        'captures': [],
        'findings': [],
        'questions': [],
    }
    return session_report.render(report)


class ChangedFileClassification(unittest.TestCase):
    def test_scaffold_host_contract_test_uses_runtime_path(self):
        paths = ['CMakeLists.txt', 'test/test_plugin.c']
        self.assertEqual(runtime_pr.classify_paths(paths), {
            'classification': 'runtime-only', 'runtime_changed': True,
        })

    def test_runtime_implementation_and_its_tests_use_runtime_path(self):
        paths = ['runtime/syslib/time.c', 'test/test_syslib.c', 'docs/runtime-syscalls.md']
        self.assertEqual(runtime_pr.classify_paths(paths), {
            'classification': 'runtime-only', 'runtime_changed': True,
        })
        self.assertEqual(runtime_pr.check_report(runtime_body(), paths), [])

    def test_runtime_build_configuration_does_not_force_decompilation_report(self):
        paths = ['runtime/syslib/time.c', 'CMakeLists.txt', 'test/CMakeLists.txt']
        self.assertEqual(runtime_pr.classify_paths(paths)['classification'], 'runtime-only')

    def test_pull_request_template_is_documentation_only(self):
        paths = ['.github/pull_request_template.md']
        self.assertEqual(runtime_pr.classify_paths(paths)['classification'], 'documentation-only')
        self.assertEqual(runtime_pr.check_report(documentation_body(), paths), [])

    def test_runtime_documentation_without_code_is_documentation_only(self):
        paths = ['docs/runtime.md', 'docs/runtime-syscalls.md', 'CONTRIBUTING.md']
        self.assertEqual(runtime_pr.classify_paths(paths), {
            'classification': 'documentation-only', 'runtime_changed': True,
        })
        self.assertEqual(runtime_pr.check_report(documentation_body(), paths), [])
        self.assertTrue(runtime_pr.check_report(runtime_body(), paths))

    def test_decompilation_only_uses_full_report(self):
        paths = ['game/kernel/example.cpp', 'symbols/functions.csv']
        self.assertEqual(runtime_pr.classify_paths(paths), {
            'classification': 'decompilation/mixed', 'runtime_changed': False,
        })
        self.assertEqual(runtime_pr.check_report(decomp_body(), paths), [])

    def test_mixed_runtime_and_decompilation_uses_full_report(self):
        paths = ['runtime/syslib/time.c', 'game/kernel/example.cpp']
        self.assertEqual(runtime_pr.classify_paths(paths), {
            'classification': 'decompilation/mixed', 'runtime_changed': True,
        })
        self.assertEqual(runtime_pr.check_report(decomp_body(), paths), [])
        self.assertTrue(runtime_pr.check_report(runtime_body(), paths))

    def test_runtime_files_cannot_downgrade_a_changed_replacement(self):
        paths = ['runtime/syslib/time.c', 'game/kernel/example.cpp']
        required = [{'address': '0x00001000', 'name': 'frame_timing_update'}]
        self.assertTrue(runtime_pr.check_report(decomp_body(verify=[]), paths, required))
        self.assertEqual(runtime_pr.check_report(decomp_body(), paths, required), [])

    def test_documentation_only_report_is_required_for_docs(self):
        paths = ['docs/ARCHITECTURE.md']
        self.assertEqual(runtime_pr.classify_paths(paths)['classification'], 'documentation-only')
        self.assertTrue(runtime_pr.check_report(runtime_body(), paths))

    def test_unknown_code_and_policy_paths_fail_closed(self):
        for path in ('tools/new_tool.py', 'runtime/settings.json', '.github/workflows/ci.yml'):
            with self.subTest(path=path):
                self.assertEqual(runtime_pr.classify_paths([path])['classification'],
                                 'decompilation/mixed')
        self.assertEqual(runtime_pr.classify_paths(['runtime/syslib/time.c', 'tools/new_tool.py'])[
            'classification'], 'decompilation/mixed')

    def test_unsafe_and_empty_path_sets_fail_closed(self):
        self.assertEqual(runtime_pr.classify_paths(['runtime/../game/x.cpp'])['classification'],
                         'decompilation/mixed')
        self.assertEqual(runtime_pr.classify_paths([])['classification'], 'decompilation/mixed')


class RuntimeReport(unittest.TestCase):
    def test_requires_clean_room_and_no_game_data(self):
        self.assertTrue(runtime_pr.check_runtime_report(runtime_body(clean_room=False)))
        self.assertTrue(runtime_pr.check_runtime_report(runtime_body(no_emulator_code=False)))
        self.assertTrue(runtime_pr.check_runtime_report(runtime_body(game_data=True)))

    def test_does_not_require_function_verification(self):
        self.assertEqual(runtime_pr.check_runtime_report(runtime_body()), [])
        self.assertTrue(runtime_pr.check_runtime_report(runtime_body(function_verify='required')))

    def test_requires_runtime_report_schema(self):
        self.assertTrue(runtime_pr.check_runtime_report(runtime_body(schema=True)))

    def test_requires_sources_tests_and_behavior_notes(self):
        self.assertTrue(runtime_pr.check_runtime_report(runtime_body(sources=[])))
        self.assertTrue(runtime_pr.check_runtime_report(runtime_body(tests=[])))
        self.assertTrue(runtime_pr.check_runtime_report(runtime_body(confirmed=[])))

    def test_rejects_unfilled_template(self):
        self.assertTrue(runtime_pr.check_runtime_report(runtime_body(calls=['replace with a call'])))
        self.assertTrue(runtime_pr.check_runtime_report('no report'))

    def test_json_must_be_inside_report_markers(self):
        body = runtime_body().replace('<!-- runtime-report:start -->\n', '')
        self.assertTrue(runtime_pr.check_runtime_report(body))


class DocumentationReport(unittest.TestCase):
    def test_requires_scope_and_summary(self):
        self.assertEqual(runtime_pr.check_documentation_report(documentation_body()), [])
        self.assertTrue(runtime_pr.check_documentation_report(documentation_body(pages=[])))
        self.assertTrue(runtime_pr.check_documentation_report(documentation_body(summary='TODO')))
        self.assertTrue(runtime_pr.check_documentation_report(
            documentation_body(implementation_changes=True)))


class ReplacementVerification(unittest.TestCase):
    replacement = [{'address': '0x00001000', 'name': 'frame_timing_update'}]

    def test_missing_verification_is_rejected(self):
        body = decomp_body(verify=[])
        self.assertTrue(any('no matching verify.py evidence' in error
                            for error in session_report.check_body(body)))

    def test_empty_verification_is_rejected(self):
        body = decomp_body(verify=[{
            'function': 'frame_timing_update', 'cases': 0, 'passed': 0, 'failed': 0,
        }])
        self.assertTrue(any('empty or incomplete' in error
                            for error in session_report.check_body(body)))

    def test_unrelated_verification_is_rejected(self):
        body = decomp_body(verify=[{
            'function': 'another_function', 'cases': 3, 'passed': 3, 'failed': 0,
        }])
        self.assertTrue(any('no matching verify.py evidence' in error
                            for error in session_report.check_body(body)))

    def test_matching_passed_verification_is_accepted(self):
        self.assertEqual(session_report.check_body(decomp_body()), [])

    def test_report_must_name_replacements_changed_in_the_pr(self):
        body = decomp_body(replaced=[], verify=[])
        errors = session_report.check_body(body, self.replacement)
        self.assertTrue(any('omits changed replacements' in error for error in errors))


if __name__ == '__main__':
    unittest.main()
