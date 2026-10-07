#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run shell commands and render them and their real output as an animated terminal GIF.

usage: terminal_gif.py OUT.gif COMMAND [COMMAND ...] [--title TEXT] [--columns N] [--rows N]

Each command is typed out at the prompt, run for real (bash, from the current directory), and its
standard output and error are shown as they were printed; a command that fails stops the
recording. Only tool output goes into the image:
use it for recordings of this repository's tools (docs/assets/ALLOWLIST names the command).
Needs Pillow and a monospaced TrueType font (DejaVu Sans Mono or Liberation Mono).
"""
import argparse
import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFont

FONTS = ('/usr/share/fonts/TTF/DejaVuSansMono.ttf', '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf',
         '/usr/share/fonts/liberation/LiberationMono-Regular.ttf')
BACKGROUND, FOREGROUND, PROMPT, TITLE = (17, 15, 20), (226, 219, 204), (176, 52, 52), (120, 112, 104)


def font(size):
    for path in FONTS:
        if os.path.isfile(path):
            return ImageFont.truetype(path, size)
    sys.exit('terminal_gif: no monospaced TrueType font found')


def run(command):
    r = subprocess.run(['bash', '-c', command], capture_output=True, text=True)
    if r.returncode:
        sys.exit(f'terminal_gif: {command!r} exited {r.returncode}; nothing rendered:\n{r.stdout}{r.stderr}')
    return (r.stdout + r.stderr).rstrip('\n').splitlines()


def wrap(lines, columns):
    out = []
    for line in lines:
        line = line.expandtabs(4)
        while len(line) > columns:
            out.append(line[:columns])
            line = line[columns:]
        out.append(line)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('out')
    ap.add_argument('commands', nargs='+')
    ap.add_argument('--title', default='')
    ap.add_argument('--columns', type=int, default=92)
    ap.add_argument('--rows', type=int, default=22)
    a = ap.parse_args()

    f = font(15)
    cw = round(f.getlength('M'))
    lh = 20
    pad = 16
    top = pad + (lh + 6 if a.title else 0)
    size = (pad * 2 + cw * a.columns, top + lh * a.rows + pad)
    screen = []          # (text, is_prompt_line)
    frames, durations = [], []

    def snap(ms, cursor=False):
        img = Image.new('P', size)
        img.putpalette(list(BACKGROUND) + list(FOREGROUND) + list(PROMPT) + list(TITLE) + [0, 0, 0] * 252)
        d = ImageDraw.Draw(img)
        if a.title:
            d.text((pad, pad), a.title, font=f, fill=3)
        rows = []
        for text, prompt in screen:
            if prompt:
                parts = [text[i:i + a.columns - 2] for i in range(0, max(len(text), 1), a.columns - 2)]
                rows += [(parts[0], True)] + [(p, None) for p in parts[1:]]
            else:
                rows.append((text, False))
        visible = rows[-a.rows:]
        for i, (text, prompt) in enumerate(visible):
            y = top + lh * i
            if prompt:
                d.text((pad, y), '$', font=f, fill=2)
                d.text((pad + 2 * cw, y), text, font=f, fill=1)
            else:
                d.text((pad + (2 * cw if prompt is None else 0), y), text, font=f, fill=1)
        if cursor and visible:
            text, prompt = visible[-1]
            x = pad + cw * (len(text) + (0 if prompt is False else 2))
            d.rectangle([x, top + lh * (len(visible) - 1) + 3, x + cw - 2, top + lh * len(visible) - 3], fill=1)
        frames.append(img)
        durations.append(ms)

    for command in a.commands:
        screen.append(('', True))
        snap(500, cursor=True)
        for i in range(0, len(command), 3):
            screen[-1] = (command[:i + 3], True)
            snap(40, cursor=True)
        output = run(command)
        for line in wrap(output, a.columns):
            screen.append((line, False))
            snap(60)
        snap(1800)
    screen.append(('', True))
    snap(3000, cursor=True)
    frames[0].save(a.out, save_all=True, append_images=frames[1:], duration=durations, loop=0, optimize=True)
    print(f'terminal_gif: {len(frames)} frames, {size[0]}x{size[1]}, {os.path.getsize(a.out)} bytes -> {a.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
