"""Check native arithmetic against Python datetime, without the source package."""
import ctypes as C
import datetime as dt
import json
import pathlib
import random
from native_bridge import Native, Delta, Relative

native = Native()
lib = native.lib
lib.fpt_timedelta_total_seconds.argtypes = [C.POINTER(Delta)]
lib.fpt_timedelta_total_seconds.restype = C.c_double
rng = random.Random(7132026)
failures = []
count = 0

for i in range(100000):
    if i < 100:
        d = dt.timedelta(days=rng.choice([-999999999, 999999998, -1, 0, 1]),
                         seconds=rng.randrange(86400), microseconds=rng.randrange(1000000))
    else:
        d = dt.timedelta(days=rng.randrange(-999999999, 999999999),
                         seconds=rng.randrange(86400), microseconds=rng.randrange(1000000))
    actual = lib.fpt_timedelta_total_seconds(C.byref(Delta(d.days, d.seconds, d.microseconds)))
    expected = d.total_seconds()
    count += 1
    if actual != expected:
        failures.append({'operation': 'total_seconds', 'input': str(d), 'actual': actual, 'expected': expected})

frames = {'year': ('days', 365), 'month': ('days', 30), 'week': ('days', 7),
          'day': ('days', 1), 'hour': ('hours', 1), 'minute': ('minutes', 1), 'second': ('seconds', 1)}
for i in range(20000):
    frame = rng.choice(list(frames))
    unit, scale = frames[frame]
    card = rng.uniform(-1000000, 1000000) if i % 2 else rng.uniform(-1, 1)
    past = bool(i % 3)
    expected = dt.timedelta(**{unit: (-card if past else card) * scale})
    result = Delta()
    native.check(lib.fpt_relative_to_timedelta(C.byref(Relative(card, None, frame.encode(), b'past' if past else b'future')), C.byref(result)))
    actual = dt.timedelta(days=result.days, seconds=result.seconds, microseconds=result.microseconds)
    count += 1
    if actual != expected:
        failures.append({'operation': 'timedelta', 'input': [card, frame, past], 'actual': str(actual), 'expected': str(expected)})

native.close()
report = {'observations': count, 'mismatches': len(failures), 'failures': failures}
output = pathlib.Path('test-results/arithmetic.json')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, indent=2), encoding='utf-8')
print(count, 'arithmetic observations;', len(failures), 'mismatches')
for failure in failures[:10]:
    print(failure)
raise SystemExit(bool(failures))
