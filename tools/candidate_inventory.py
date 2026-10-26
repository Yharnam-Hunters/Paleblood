#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Private recorded-fixture inventories and fail-closed automation preflight reports."""
from __future__ import annotations

from collections import Counter
import hashlib
import json
from pathlib import Path

import differential
import function_catalog as catalog
import function_qualification as qualification

VERSION = 1
MAX_CASE_BYTES = 16 << 20
MAX_CASES = 20_000


def digest(value) -> str:
    return hashlib.sha256(catalog.canonical(value).encode()).hexdigest()


def provenance_blockers(cases: list[dict], name: str, executable_sha: str | None) -> list[str]:
    problems = []
    for case in cases:
        provenance = case.get('capture')
        identifier = str(case.get('id', '?'))
        if not isinstance(name, str) or not name or not isinstance(provenance, dict) or provenance.get('function') != name or \
                not isinstance(provenance.get('run_id'), str) or not provenance['run_id'].strip():
            problems.append(f'{identifier}: function/run_id capture provenance unavailable')
        elif not executable_sha or provenance.get('executable_sha256') != executable_sha:
            problems.append(f'{identifier}: capture executable identity missing or mismatched')
        if case.get('fixture_generation') or case.get('generated_from') or case.get('exploration'):
            problems.append(f'{identifier}: generated fixture is not recorded gameplay evidence')
    return problems


def current_tools(root: Path) -> dict:
    paths = ('tools/harness.py', 'tools/instruction_trace.py', 'tools/verify.py', 'runtime/loader.c',
             'tools/differential.py', 'tools/call_arguments.py')
    names = ('harness_sha256', 'instruction_trace_sha256', 'verify_sha256', 'loader_sha256',
             'engine_sha256', 'call_arguments_sha256')
    return {name: catalog.sha256(root / path) for name, path in zip(names, paths)}


def original_trace_status(bundle: dict | None, *, address: str, executable_sha: str,
                          fixture_sha: str, abi: dict | None, ids: list[str], root: Path,
                          file_ids: list[str] | None = None) -> dict:
    if bundle is None:
        return {'status': 'unavailable', 'cfg_feedback_eligible': False,
                'blockers': ['original instruction/CFG evidence unavailable']}
    if not isinstance(bundle, dict) or not isinstance(bundle.get('identity'), dict) or not executable_sha:
        return {'status': 'unavailable', 'cfg_feedback_eligible': False,
                'blockers': ['execution/executable identity unavailable']}
    identity = bundle.get('identity', {})
    expected = {'address': address, 'executable_sha256': executable_sha, 'fixture_sha256': fixture_sha,
                'abi_sha256': digest(abi) if abi else None, **current_tools(root)}
    if any(identity.get(key) != value for key, value in expected.items()):
        return {'status': 'stale', 'cfg_feedback_eligible': False,
                'blockers': ['execution evidence identity differs from executable, fixtures, ABI or current tools']}
    result = bundle.get('result', {})
    if not isinstance(result, dict):
        return {'status': 'incomplete', 'cfg_feedback_eligible': False, 'blockers': ['execution evidence malformed']}
    if file_ids is not None:
        ids = file_ids  # Both batch runners use file stems as fixture identities.
    execution = result.get('original', result.get('execution', {}))
    executions = execution.get('cases') if isinstance(execution, dict) else None
    if not isinstance(executions, list) or any(not isinstance(case, dict) or not isinstance(case.get('id'), str) for case in executions):
        return {'status': 'incomplete', 'cfg_feedback_eligible': False, 'blockers': ['execution cases malformed']}
    by_id = {case.get('id'): case for case in executions}
    problems = []
    if len(by_id) != len(executions) or set(by_id) != set(ids) or not ids:
        problems.append('original evidence does not cover exactly the fixture inventory')
    for case in executions:
        trace = case.get('instruction_trace', {})
        if not isinstance(trace, dict) or not isinstance(trace.get('control_flow_coverage'), dict):
            problems.append('instruction/CFG observations unavailable or malformed')
            continue
        coverage = trace.get('control_flow_coverage', {})
        if case.get('fault') or case.get('errors') or trace.get('complete') is not True or \
                trace.get('gaps') or trace.get('skipped_host_calls') or trace.get('external_transitions') or trace.get('import_observations'):
            problems.append('original instruction trace incomplete or external execution unobserved')
        if coverage.get('status') != 'complete' or coverage.get('feedback_eligible') is not True or \
                not coverage.get('blocks') or not isinstance(coverage.get('edges'), list):
            problems.append('original CFG evidence incomplete or ineligible for feedback')
        if not isinstance(trace.get('events'), list) or not trace.get('events') or not isinstance(trace.get('instructions'), int) or trace['instructions'] <= 0:
            problems.append('original instruction/memory observations unavailable')
        for event in trace.get('events', []) if isinstance(trace.get('events'), list) else []:
            if not isinstance(event, dict):
                problems.append('memory access observation malformed')
                continue
            if event.get('valid_region') is not True or event.get('supported') is False or \
                    not isinstance(event.get('width'), int) or not 1 <= event['width'] <= 64 or \
                    event.get('access') not in ('read', 'write', 'read_write') or event.get('before') is None or \
                    (event.get('access') in ('write', 'read_write') and event.get('after') is None):
                problems.append('original memory access is unsupported, out of bounds or unavailable')
    return {'status': 'complete' if not problems else 'incomplete', 'cfg_feedback_eligible': not problems,
            'blockers': sorted(set(problems)), 'evidence_sha256': digest(bundle)}


def inspect(function: dict, directory: Path | None, metadata: dict, ownership: dict,
            decision: dict | None, *, minimum: int = 32, trace_bundle: dict | None = None,
            root: Path = differential.ROOT) -> dict:
    if minimum < 1:
        raise ValueError('minimum recorded fixtures must be positive')
    address = function['address']
    directory_available = directory is not None and directory.is_dir()
    recorded, hashes, ids, file_ids, invalid = [], [], [], [], []
    abi = None
    edge_count = None
    if directory_available:
        edge_count = len(list((directory / 'edge').glob('*.json')))
        paths = sorted(p for p in directory.glob('*.json') if p.name != 'abi.json')
        if len(paths) > MAX_CASES:
            invalid.append('fixture inventory exceeds bounded file count')
            paths = paths[:MAX_CASES]
        abi_path = directory / 'abi.json'
        if abi_path.is_file():
            if abi_path.stat().st_size > MAX_CASE_BYTES:
                invalid.append('ABI manifest exceeds size bound')
            else:
                try:
                    abi = json.loads(abi_path.read_text())
                except (ValueError, OSError):
                    invalid.append('ABI manifest unreadable or malformed')
        for path in paths:
            if path.stat().st_size > MAX_CASE_BYTES:
                invalid.append('fixture exceeds size bound')
                continue
            hashes.append(catalog.sha256(path))
            try:
                case = json.loads(path.read_text())
                if case.get('schema') != 1 or catalog.address(case['address']) != address:
                    raise ValueError('fixture address/schema mismatch')
                differential.validate_fixture_case(case, 'local fixture')
                if case.get('abi') is not None:
                    if abi is not None and abi != case['abi']:
                        invalid.append('conflicting fixture ABI manifests')
                    abi = case['abi']
                recorded.append(case)
                ids.append(case.get('id', path.stem))
                file_ids.append(path.stem)
            except (ValueError, KeyError, TypeError, AttributeError):
                invalid.append('fixture malformed, invalid or for another function')
    fixture_sha = hashlib.sha256(''.join(hashes).encode()).hexdigest()
    abi_blockers = differential.validate_abi(abi)
    if not abi_blockers:
        # Reuse the established source-reference policy; no separate approval standard.
        from automation_pipeline import validate_abi_evidence
        abi_blockers += validate_abi_evidence(abi['evidence'], root)
    provenance = provenance_blockers(recorded, function.get('name'), metadata.get('executable_sha256'))
    trace = original_trace_status(trace_bundle, address=address, executable_sha=metadata.get('executable_sha256'),
                                  fixture_sha=fixture_sha, abi=abi, ids=ids, file_ids=file_ids, root=root)
    blockers = []
    if function.get('replacement_status') in ('replaced', 'edge-verified', 'verified') or ownership.get('status') == 'already_implemented':
        blockers.append('already implemented; regression-only, not a new replacement candidate')
    if ownership.get('status') != 'confirmed_unclaimed' or ownership.get('blockers'):
        blockers.append('ownership is not freshly confirmed unclaimed')
    if not (decision or {}).get('eligible_for_replacement_development'):
        blockers.append('current qualification does not allow replacement development')
    if directory is None or not directory.is_dir():
        blockers.append('recorded fixture directory unavailable')
    if len(recorded) < minimum:
        blockers.append(f'insufficient recorded fixtures: {len(recorded)} < {minimum}')
    capture_ids = [(c['capture'].get('run_id'), c.get('id')) for c in recorded
                   if isinstance(c.get('capture'), dict) and isinstance(c['capture'].get('run_id'), str)
                   and isinstance(c.get('id'), str)]
    if len(set(capture_ids)) != len(capture_ids):
        invalid.append('duplicate capture identity within one recorded run')
    inputs_ready = not (blockers or abi_blockers or invalid or provenance)
    all_blockers = sorted(set(blockers + abi_blockers + invalid +
                             (['recorded provenance/executable identity unavailable or invalid'] if provenance else []) + trace['blockers']))
    binding = {'catalog_identity': metadata.get('catalog_identity'), 'function_key': function.get('cache_key'),
               'ownership': ownership.get('snapshot_sha256'), 'qualification': digest(decision),
               'fixtures': fixture_sha, 'abi': digest(abi), 'trace': digest(trace_bundle),
               'tools': current_tools(root), 'inventory_source': catalog.sha256(Path(__file__)), 'minimum': minimum}
    return {'address': address, 'name': function.get('name'), 'status': function.get('replacement_status'),
            'executable_sha256': metadata.get('executable_sha256'), 'ownership': ownership,
            'abi': {'confidence': abi.get('confidence') if isinstance(abi, dict) else 'unknown',
                    'evidence': abi.get('evidence') if isinstance(abi, dict) else None, 'blockers': abi_blockers},
            'root_fixture_count': len(recorded) if directory_available else None,
            'recorded_fixture_count': sum(not provenance_blockers([c], function.get('name'), metadata.get('executable_sha256'))
                                           for c in recorded) if directory_available else None,
            'synthetic_edge_count': edge_count, 'fixture_provenance': {'valid': not provenance and bool(recorded),
                'problems': len(provenance), 'recorded_dates_known': sum(isinstance(c.get('capture'), dict) and bool(c['capture'].get('recorded_at')) for c in recorded)},
            'instruction_trace': trace, 'classification': function.get('classification', {'category': 'unknown'}),
            'type_context': function.get('type_intelligence', {}), 'qualification': (decision or {}).get('classification', 'unavailable'),
            'trial_inputs_ready': inputs_ready, 'automation_trial_eligible': inputs_ready and trace['status'] == 'complete',
            'blockers': all_blockers, 'binding': binding, 'report_identity': digest(binding),
            'memory_limitations': ['Host/import snapshots and scripted accesses do not establish instruction coverage',
                                   'Only supported complete instruction-memory and CFG evidence can qualify a trial']}


def save_reports(state: Path, rows: list[dict]) -> None:
    db = catalog.connect(state)
    try:
        db.execute('CREATE TABLE IF NOT EXISTS candidate_inventories (address TEXT PRIMARY KEY, identity TEXT NOT NULL, data TEXT NOT NULL)')
        with db:
            db.executemany('INSERT INTO candidate_inventories VALUES (?, ?, ?) ON CONFLICT(address) DO UPDATE SET '
                           'identity=excluded.identity,data=excluded.data',
                           [(r['address'], r['report_identity'], catalog.canonical(r)) for r in rows])
    finally:
        db.close()


def campaign(state: Path, roots: list[Path], *, addresses: list[str] | None = None,
             minimum: int = 32, limit: int = 20, evidence_paths: list[Path] = ()) -> dict:
    if not 1 <= limit <= 1000:
        raise ValueError('ranked candidate limit must be between 1 and 1000')
    functions, edges, metadata = catalog.load_snapshot(state)
    decisions = {r['address']: r for r in qualification.load_qualifications(state, qualification.qualification_tool_identity(differential.ROOT))}
    snapshot = qualification.load_ownership(state)
    by_name = {r['name']: r for r in functions if r.get('name')}
    by_address = {r['address']: r for r in functions}
    directories, ambiguous, unrecognized = {}, set(), []
    for parent in roots:
        candidates = [parent] if parent.name in by_name else sorted(parent.iterdir()) if parent.is_dir() else []
        for directory in candidates:
            if not directory.is_dir():
                continue
            row = by_name.get(directory.name)
            if row is None:
                first = next((p for p in sorted(directory.glob('*.json')) if p.name != 'abi.json'), None)
                if first and first.stat().st_size <= MAX_CASE_BYTES:
                    try:
                        row = by_address.get(catalog.address(json.loads(first.read_text())['address']))
                    except (ValueError, KeyError, TypeError, AttributeError):
                        pass
            if row is None:
                unrecognized.append(directory.name)
            elif row['address'] in directories and directories[row['address']].resolve() != directory.resolve():
                ambiguous.add(row['address'])
            else:
                directories[row['address']] = directory
    bundles = {}
    for path in evidence_paths:
        data = json.loads(path.read_text())
        key = data.get('identity', {}).get('address')
        if key in bundles:
            raise ValueError('multiple execution evidence bundles for one function; select one explicitly')
        bundles[key] = data
    selected = {catalog.address(a) for a in addresses} if addresses else set(directories) | set(decisions)
    if selected - set(by_address):
        raise ValueError('requested candidate is absent from the catalog')
    if not addresses:
        selected.update(r['address'] for r in catalog.rank_functions(functions, edges, limit))
    rows = []
    for function in functions:
        if function['address'] not in selected:
            continue
        owner = qualification.classify_ownership(function['address'], function.get('name'), snapshot, function.get('system'))
        directory = None if function['address'] in ambiguous else directories.get(function['address'])
        rows.append(inspect(function, directory, metadata, owner, decisions.get(function['address']), minimum=minimum,
                            trace_bundle=bundles.get(function['address'])))
    save_reports(state, rows)
    blockers = Counter(reason for row in rows for reason in row['blockers'])
    row_map = {r['address']: r for r in rows}
    ranked = catalog.rank_functions([{**f, 'ownership': row_map[f['address']]['ownership'],
                                     'qualification': decisions.get(f['address'])}
                                    for f in functions if f['address'] in row_map], edges, limit)
    return {'schema': VERSION, 'catalog_identity': metadata['catalog_identity'], 'catalog_functions': len(functions),
            'candidate_scope': 'explicit addresses' if addresses else 'all recognized capture sets, current campaign and ranked subset',
            'candidates_inspected': len(rows), 'capture_sets': len(directories), 'ambiguous_capture_sets': len(ambiguous),
            'unrecognized_capture_sets': sorted(set(unrecognized)), 'roots_available': [p.is_dir() for p in roots],
            'trial_inputs_ready': sum(r['trial_inputs_ready'] for r in rows),
            'automation_trial_eligible': sum(r['automation_trial_eligible'] for r in rows),
            'blocker_totals': dict(sorted(blockers.items())), 'ranked_candidates': [
                {**r, 'automation_trial_eligible': row_map[r['address']]['automation_trial_eligible'],
                 'blockers': row_map[r['address']]['blockers']} for r in ranked], 'rows': rows}


def require_current_report(saved: dict, current: dict) -> None:
    if saved.get('report_identity') != current['report_identity'] or saved.get('binding') != current['binding']:
        raise ValueError('eligibility report is stale for ownership, qualification, fixtures, ABI, analysis or tools')
    if not current['automation_trial_eligible']:
        raise ValueError('automation trial blocked: ' + '; '.join(current['blockers']))
