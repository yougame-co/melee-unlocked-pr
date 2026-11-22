#!/usr/bin/env python3
"""The online menu's two pages' row words ("YouGame <mode>", "Slippi <mode>", "Both Unranked") in
Slippi's own label style, from the Slippi-matched label setter (E:/mu-yg/labels/bake_slippi_labels.py,
read only). Written as sourceport/game/shim/mu_online_label_art.h with mu_sst_* symbols.
"YouGame Squad Strike" is the owner's pick of 11-18 (row style R of E:/mu-yg/labels/bake_row_styles.py):
one line at the largest size whose ink fits 194 texel columns, letter gaps 0.6 texel tighter, on a label
SQUAD_W texels wide centred SQUAD_SHIFT texels right of the native label (the game widens its quad).
Unranked's three slider values (owner 11-19: one size, and not out of place beside the Slippi page's
rows): "YouGame Unranked", "Both Unranked" and "Slippi Unranked" at one scale, the largest whose
"YouGame Unranked" ink fits UNRANKED_INK columns (letter gaps 0.6 texel tighter), on one baseline, each
centred on a label MU_SST_UNRANKED_W texels wide (the game widens its quad to match, centred on the
native label). "Slippi Direct", "Slippi Teams" and "Slippi Party" are set again at that same scale
(mu_sst_page_*), so every row of the Slippi page has one letter height.
Usage: py -3.12 tools/bake_online_label_art.py [--labels E:/mu-yg/labels]"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXTRA = [('both_unranked', 'Both Unranked')]
KEEP = {'yougame_ranked', 'yougame_unranked', 'yougame_friends', 'yougame_doubles', 'yougame_squad', 'slippi_unranked', 'slippi_direct', 'slippi_teams', 'slippi_party', 'both_unranked'}
SQUAD_INK, SQUAD_TRACKING, SQUAD_SHIFT = 194, -0.6, 12
# 186: the ink Squad Strike shows (194 columns drawn on a 192-texel quad from 200), which clears the ring.
UNRANKED_INK, UNRANKED_TRACKING, UNRANKED_BASE = 186, -0.6, 24
# 11-21: the YouGame page's rows drop the "YouGame" prefix, the page's heading already says YouGame.
UNRANKED = [('un_yougame', '', 'Unranked'), ('un_both', 'Both', 'Unranked'), ('un_slippi', 'Slippi', 'Unranked')]
# The YouGame page's other rows, set at the same scale as every other row (one letter height on both pages).
YOUGAME_ROWS = {'yougame_ranked': 'Ranked', 'yougame_friends': 'Friends', 'yougame_doubles': 'Doubles'}
PAGE = [('page_slippi_direct', 'Slippi', 'Direct'), ('page_slippi_teams', 'Slippi', 'Teams'),
        ('page_slippi_party', 'Slippi', 'Party')]


def one_line(rs, st, text, m, tracking):
    """One line at scale m on the shared baseline (a word without descenders sits on the same line)."""
    import numpy as np
    F = np.zeros((rs.bl.H, 2 * rs.MARGIN + rs.bl.W)); A = np.zeros_like(F)
    Fp, Ap = rs.piece(st, text, m, UNRANKED_BASE, tracking)
    rs.paste(F, A, Fp, Ap, rs.MARGIN)
    return F, A


def unranked_set(st):
    """[(symbol, text, IA4 tiled bytes, width, description)] for the Unranked trio and the page rows."""
    import bake_row_styles as rs  # noqa: E402
    m = 1.0
    while True:   # the largest scale at which every slider value fits
        inks = []
        for _, prefix, word in UNRANKED:
            F, A = one_line(rs, st, f'{prefix} {word}'.strip(), m, UNRANKED_TRACKING)
            x0, x1 = rs.ink_cols(A)
            inks.append((x1 - x0, A))
        if max(i for i, _ in inks) <= UNRANKED_INK:
            break
        m = round(m - 0.01, 2)
    w = rs.width_for(max(inks, key=lambda t: t[0])[1])
    out = []
    for sym, prefix, word in UNRANKED + PAGE:
        trio = sym.startswith('un_')
        F, A = one_line(rs, st, f'{prefix} {word}'.strip(), m, UNRANKED_TRACKING if trio else 0.0)
        x0, x1 = rs.ink_cols(A)
        width = w if trio else rs.bl.W
        assert x1 - x0 <= (UNRANKED_INK if trio else rs.USABLE), (sym, x1 - x0)
        Fc, Ac = rs.place(F, A, width)
        a, l = rs.bl.to_ia4(Fc, Ac)
        out.append((sym, f'{prefix} {word}'.strip(), rs.tiled(a, l, width), width,
                    f'scaled {m:.2f}, tracking {UNRANKED_TRACKING if trio else 0.0}, {x1 - x0} ink columns, {width} texels'))
    return out, w, m


def page_row(st, text, m):
    """A YouGame page row at the shared scale m, centred on a native label."""
    import bake_row_styles as rs  # noqa: E402
    F, A = one_line(rs, st, text, m, 0.0)
    x0, x1 = rs.ink_cols(A)
    assert x1 - x0 <= rs.USABLE, (text, x1 - x0)
    Fc, Ac = rs.place(F, A, rs.bl.W)
    a, l = rs.bl.to_ia4(Fc, Ac)
    return rs.tiled(a, l, rs.bl.W), f'scaled {m:.2f}, tracking 0.0, {x1 - x0} ink columns, {rs.bl.W} texels'


def squad_wide(st, m):
    """Squad Strike's wide label: (IA4 tiled bytes, width in texels, description)."""
    import bake_row_styles as rs  # noqa: E402
    F, A = one_line(rs, st, 'Squad Strike', m, SQUAD_TRACKING)
    x0, x1 = rs.ink_cols(A)
    assert x1 - x0 <= SQUAD_INK, x1 - x0
    w = max(rs.bl.W, rs.width_for(A))
    Fc, Ac = rs.place(F, A, w)
    a, l = rs.bl.to_ia4(Fc, Ac)
    return rs.tiled(a, l, w), w, f'scaled {m:.2f}, tracking {SQUAD_TRACKING}, {x1 - x0} ink columns, {w} texels'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--labels', default=r'E:\mu-yg\labels')
    ap.add_argument('--output', type=Path, default=ROOT / 'sourceport/game/shim/mu_online_label_art.h')
    a = ap.parse_args()
    sys.path.insert(0, a.labels)
    import bake_slippi_labels as b  # noqa: E402
    labels = b.load_slippi_labels(b.DEFAULT_ISO, str(b.DEFAULT_SYS))
    st = b.Setter(labels)
    lines = ['/* Generated by tools/bake_online_label_art.py from E:/mu-yg/labels/bake_slippi_labels.py; do not edit. */',
             '/* Online menu rows in Slippi 3.7.0\'s own label style (letters from Slippi\'s labels). */',
             '#ifndef MU_ONLINE_LABEL_ART_H', '#define MU_ONLINE_LABEL_ART_H']
    rows, w, m = unranked_set(st)
    for sym, text in [w for w in b.WORDS + EXTRA if w[0] in KEEP]:
        size = 'MU_LABEL_BYTES'
        if sym == 'yougame_squad':
            data, sw, how = squad_wide(st, m)
            text = 'Squad Strike'
            lines.append("/* Based on Squad Strike by Chris (x9x9x9x9x9), https://github.com/x9x9x9x9x9/squad-strike v1.0.0, "
                         "used with the author's permission (11-03-2026). */")
            lines.append(f'#define MU_SST_SQUAD_W {sw}        /* texels; the game widens the label quad to match */')
            lines.append(f'#define MU_SST_SQUAD_SHIFT {SQUAD_SHIFT if sw > 176 else 0}    /* texels right of the native label centre */')
            size = 'MU_SST_SQUAD_W * 32'
        elif sym in YOUGAME_ROWS:
            text = YOUGAME_ROWS[sym]
            data, how = page_row(st, text, m)
        else:
            F, A, how, det = b.bake(st, text)
            al, ll = b.to_ia4(F, A)
            data = b.tiled(al, ll)
        lines.append(f'/* "{text}" ({how}) */')
        lines.append(f'static const unsigned char mu_sst_{sym}[{size}] __attribute__((aligned(32), unused)) = {{')
        lines.extend('    ' + ','.join(f'0x{v:02x}' for v in data[i:i + 32]) + ',' for i in range(0, len(data), 32))
        lines.append('};')
        print(f'{sym:16s} {text:22s} {how}')
    lines.append(f'#define MU_SST_UNRANKED_W {w}   /* texels; the game widens the Unranked label quad to match */')
    for sym, text, data, width, how in rows:
        size = 'MU_SST_UNRANKED_W * 32' if width == w and sym.startswith('un_') else 'MU_LABEL_BYTES'
        lines.append(f'/* "{text}" ({how}) */')
        lines.append(f'static const unsigned char mu_sst_{sym}[{size}] __attribute__((aligned(32), unused)) = {{')
        lines.extend('    ' + ','.join(f'0x{v:02x}' for v in data[i:i + 32]) + ',' for i in range(0, len(data), 32))
        lines.append('};')
        print(f'{sym:20s} {text:22s} {how}')
    lines += ['#endif', '']
    a.output.write_bytes('\n'.join(lines).encode('utf-8'))   # LF on every system


if __name__ == '__main__':
    main()
