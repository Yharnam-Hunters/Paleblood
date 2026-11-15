# SPDX-License-Identifier: GPL-2.0-or-later
"""Owned ELF input for the actual run_boot pending-close/qualification-failure scope seam."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import ps4elf


def main():
    binary = Path(os.environ['BB_BOOT_SCOPE'])
    if not binary.is_file():
        raise RuntimeError('selected actual boot scope executable is missing')
    with tempfile.TemporaryDirectory(prefix='owned-window-scope-') as folder:
        root = Path(folder)
        image = root / 'owned.elf'
        image.write_bytes(ps4elf.build([(0, b'\xc3' + b'\x90' * 31, 4096, 5)], [], []))
        prefix = root / 'status'
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=12,
            env=dict(os.environ, BB_SCOPE_WINDOW_REAL_BOOT=str(image), BB_SCOPE_WINDOW_STATUS=str(prefix)))
        print(result.stdout, end='')
        if result.returncode:
            raise RuntimeError('actual window boot scope failed: ' + result.stderr)
        if 'Darwin actual window boot: 3 cases, 0 failures' not in result.stdout:
            raise RuntimeError('actual window boot scope is incomplete')
        status = json.loads(Path(str(prefix) + '-23.json').read_text())
        if status['phase'] != 'eboot' or status['outcome'] != 'runtime stop' or status['imports_called'] != 0:
            raise RuntimeError('actual pending-close report has wrong phase/outcome/import effects')
        if status['runtime_stop'] != {'function': 'owned GUI close', 'reason': 'complete close payload'}:
            raise RuntimeError('actual pending-close report lost its complete first payload')
        for number in (24, 32):
            if Path(str(prefix) + f'-{number}.json').exists():
                raise RuntimeError('real qualification failure incorrectly produced a normal report')
        if result.stdout.count('Darwin actual window boot qualifier timeout: return3, no report') != 1:
            raise RuntimeError('actual owner-timer qualification failure is missing')
        print('Owned actual run_boot: pending Results and genuine fault/timeout qualification gates passed')


if __name__ == '__main__':
    main()
