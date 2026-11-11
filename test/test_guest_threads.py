# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute our own AMD64 TLS load/store in the production worker and loader paths."""
from pathlib import Path
import subprocess
import sys
import tempfile
from ps4elf import build

# mov rax,fs:0; publish TCB to input[0]; copy input[1] through TCB[-8]; return it.
# The production loader must rewrite the FS load to the qualified GS displacement.
CODE = bytes.fromhex('64488b042500000000 488907 488b5708 488950f8 488b40f8 c3')


def main():
    with tempfile.TemporaryDirectory(prefix='owned-guest-worker-') as folder:
        image = Path(folder) / 'worker.elf'
        image.write_bytes(build([(0, CODE, 0x1000, 5)], [], []))
        result = subprocess.run([sys.argv[1], str(image)], check=False, timeout=25)
        return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
