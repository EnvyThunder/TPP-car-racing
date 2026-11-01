# Draws the training curves of the four brains (evolve_<k>.csv) into one picture.
#   python tools/evolve/plot_curves.py build/evolve docs/cpu-psychology/m5_training.png [rules_m_per_s] [switches]
# switches: for each species, the generation where the test races went from 75 s to the
# whole 2 laps (125 s), e.g. 120,150,150,150 - the speed panel divides by the right time.
import csv
import sys

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

src, out = sys.argv[1], sys.argv[2]
rules = float(sys.argv[3]) if len(sys.argv) > 3 else None
switch = [int(x) for x in sys.argv[4].split(',')] if len(sys.argv) > 4 else [10 ** 9] * 4
NAMES = ['RACER', 'DAREDEVIL', 'BRUISER', 'SMOOTH']
COLOURS = ['#2a6fdb', '#e8590c', '#c92a2a', '#2b8a3e']

fig, (a1, a2) = plt.subplots(1, 2, figsize=(11, 4.2))
for k, name in enumerate(NAMES):
    try:
        rows = list(csv.DictReader(open('%s/evolve_%d.csv' % (src, k))))
    except OSError:
        continue
    g = [int(r['generation']) for r in rows]
    a1.plot(g, [float(r['best']) for r in rows], color=COLOURS[k], lw=1.6, label=name + ' best')
    a1.plot(g, [float(r['mean']) for r in rows], color=COLOURS[k], lw=0.8, ls='--', alpha=0.7)
    speed = [float(r['progress_best']) / (75.0 if int(r['generation']) < switch[k] else 125.0) for r in rows]
    a2.plot(g, speed, color=COLOURS[k], lw=1.6, label=name)
    if switch[k] < 10 ** 9:
        for a in (a1, a2):
            a.axvline(switch[k], color=COLOURS[k], lw=0.7, ls=':')
if rules:
    a2.axhline(rules, color='#555', lw=1.2, ls='--', label='rule-based driver')
a1.set_title('Score per generation (solid: best, dashed: average)')
a1.set_xlabel('generation  (dotted: test races become the whole 2 laps)')
a1.set_ylabel('score (its own species scoring)')
a1.legend(fontsize=8)
a2.set_title('Best brain: average speed in the test races')
a2.set_xlabel('generation')
a2.set_ylabel('metres per second (6 maps)')
a2.legend(fontsize=8)
for a in (a1, a2):
    a.grid(alpha=0.3)
fig.tight_layout()
fig.savefig(out, dpi=110)
print('wrote', out)
