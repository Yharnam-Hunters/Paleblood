#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Play a pad route during a game run (started by tools/run_game.sh for $BB_ROUTE).

usage: play_route.py ROUTE PAD_FILE FRAMES_DIR [DISPLAY]

A route is a list of lines, played in order ('#' starts a comment):

  50 cross               at 50 s after the start, press cross (bbport pad file tokens:
                         cross circle square triangle up down left right options l1 ... lx=0..255)
  +2 down hold=1         2 s after the previous line, hold down for 1 s (default hold 0.3 s)
  +1 shot                save a screenshot (frames/<run>/rNNNN.N.png)
  wait "Log In" 60       block until the text is on screen (OCR, case-insensitive; "a|b" waits
                         for either), at most 60 s
                         (default 90); on timeout the route stops with a message
  wait "Next" 60 cross every=4
                         while waiting, press cross every 4 s (gets through dialogs and default
                         menu entries whatever their number)
  wait "300" 60 nofail   on timeout, go on with the next line instead of stopping
  +1 cross shot          tokens and shot combine
  +1 type=Hunter key=Return
                         keyboard input on DISPLAY (xdotool), for text boxes such as name entry

Waiting on text keeps a route working when loading times or dialogs differ between runs. The
text is read with tesseract from a screenshot of DISPLAY; a highlighted menu entry often reads
badly, so wait on a neighbouring line instead.
"""
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time


def log(msg: str) -> None:
    print(f'route: {msg}', flush=True)


def screenshot(display: str, path: str) -> bool:
    env = dict(os.environ, DISPLAY=display)
    return subprocess.run(['import', '-window', 'root', path], env=env,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def ocr_language() -> str:
    """English when its data is installed, else any installed Latin-script language."""
    r = subprocess.run(['tesseract', '--list-langs'], capture_output=True, text=True)
    langs = [w.strip() for w in r.stdout.splitlines()[1:] if w.strip() and w.strip() != 'osd'] if r.returncode == 0 else []
    return 'eng' if 'eng' in langs or not langs else langs[0]


LANG = None


def screen_text(display: str) -> str:
    global LANG
    LANG = LANG or ocr_language()
    with tempfile.TemporaryDirectory() as d:
        raw, prep = os.path.join(d, 'raw.png'), os.path.join(d, 'prep.png')
        if not screenshot(display, raw):
            return ''
        # Light text on dark backgrounds: grey, inverted, enlarged reads best.
        subprocess.run(['magick', raw, '-colorspace', 'gray', '-negate', '-resize', '200%', prep],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        r = subprocess.run(['nice', '-n', '10', 'tesseract', prep, '-', '-l', LANG, '--psm', '11'],
                           capture_output=True, text=True)
        return ' '.join(r.stdout.split()).lower()


def keyboard(display: str, kind: str, value: str) -> None:
    env = dict(os.environ, DISPLAY=display)
    cmd = ['xdotool', 'type', '--delay', '120', value] if kind == 'type' else ['xdotool', 'key', value]
    subprocess.run(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.3)


def press(pad: str, tokens: list, hold: float) -> None:
    with open(pad, 'w') as f:
        f.write(' '.join(tokens) + '\n')
    time.sleep(hold)
    open(pad, 'w').close()


def main() -> int:
    if len(sys.argv) not in (4, 5):
        print(__doc__, file=sys.stderr)
        return 2
    route, pad, frames = sys.argv[1:4]
    display = sys.argv[4] if len(sys.argv) == 5 else ''
    start = last = time.monotonic()
    for n, line in enumerate(open(route), 1):
        words = shlex.split(line, comments=True)
        if not words:
            continue
        if words[0] == 'wait':
            text = words[1].lower()
            rest = words[2:]
            timeout = float(rest.pop(0)) if rest and re.fullmatch(r'\d+(\.\d+)?', rest[0]) else 90.0
            every = next((float(w[6:]) for w in rest if w.startswith('every=')), 0.0)
            nofail = 'nofail' in rest
            keys = [w for w in rest if not w.startswith('every=') and w != 'nofail']
            next_press = time.monotonic() + every
            if not display:
                log(f'line {n}: wait needs a display; stopping')
                return 1
            deadline = time.monotonic() + timeout
            while not any(t in screen_text(display) for t in text.split('|')):
                if time.monotonic() > deadline and nofail:
                    log(f'line {n}: "{words[1]}" not on screen after {timeout:g} s; going on (nofail)')
                    seen = False
                    break
                if time.monotonic() > deadline:
                    log(f'line {n}: "{words[1]}" not on screen after {timeout:g} s; stopping')
                    if frames:
                        os.makedirs(frames, exist_ok=True)
                        screenshot(display, os.path.join(frames, f'route-stuck-line{n}.png'))
                    return 1
                if keys and every and time.monotonic() >= next_press:
                    press(pad, keys, 0.3)
                    next_press = time.monotonic() + every
                time.sleep(0.5)
            else:
                seen = True
            last = time.monotonic()
            if seen:
                log(f'line {n}: "{words[1]}" on screen at {last - start:.1f} s')
            continue
        at = words[0]
        if re.fullmatch(r'\+\d+(\.\d+)?', at):
            target = last + float(at[1:])
        elif re.fullmatch(r'\d+(\.\d+)?', at):
            target = start + float(at)
        else:
            log(f'line {n}: expected a time, +delay or wait: {line.strip()}')
            return 2
        time.sleep(max(0.0, target - time.monotonic()))
        last = time.monotonic()
        hold, shot, tokens, keys = 0.3, False, [], []
        for w in words[1:]:
            if w.startswith(('type=', 'key=')):
                keys.append(tuple(w.split('=', 1)))
            elif w.startswith('hold='):
                hold = float(w[5:])
            elif w == 'shot':
                shot = True
            else:
                tokens.append(w)
        if shot and display:
            os.makedirs(frames, exist_ok=True)
            screenshot(display, os.path.join(frames, f'r{last - start:06.1f}.png'))
        for kind, value in keys:
            if display:
                keyboard(display, kind, value)
        if tokens:
            press(pad, tokens, hold)
    log('done')
    return 0


if __name__ == '__main__':
    sys.exit(main())
