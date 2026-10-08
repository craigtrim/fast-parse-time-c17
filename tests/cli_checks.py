"""Standalone CLI contract, including native Windows Unicode arguments/stdin."""
import json
import os
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
exe = os.environ.get('FPT_CLI', str(root/'build'/('fpt.exe' if os.name == 'nt' else 'fpt')))

def run(*args, data=None, code=0):
    result = subprocess.run([exe, *args], input=data, capture_output=True)
    assert result.returncode == code, (args, result.returncode, result.stdout, result.stderr)
    return result.stdout.decode('utf-8')

assert '1.5.0-c17.1' in run('--version')
assert 'Usage:' in run('--help')
parsed = json.loads(run('2014–2015 and 2.5h ago'))
assert parsed['explicit_dates'] == [{'text': '2014-2015', 'date_type': 'YEAR_RANGE'}]
assert parsed['relative_times'] == [{'cardinality': 2, 'frame': 'hour', 'tense': 'past'}]
assert json.loads(run('--calendar', '11/28/2025 4/14/2026 March 13'))[-1]['date'] == '2026-03-13'
assert json.loads(run('--calendar', '--date-order', 'dmy', '--range', '2026-01-01', '2026-12-31', '3/4'))[0]['date'] == '2026-04-03'
original = 'March\r\n13 and March\t13'
assert json.loads(run('--date-strings', data=original.encode())) == ['March\r\n13', 'March\t13']
lines = run('--lines', data=b'5 days ago\r\nnext week\n').splitlines()
assert len(lines) == 2 and all(json.loads(line)['has_dates'] for line in lines)
assert not json.loads(run(data=b''))['has_dates']
assert 'error' in json.loads(run(data=b'\xff', code=1))
run('--date-order', 'invalid', code=2)
print('Standalone CLI checks passed')
