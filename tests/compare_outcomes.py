"""Require the native and Python suites to have exactly the same test outcomes."""
import argparse
import json
import pathlib

p=argparse.ArgumentParser()
p.add_argument('directory',nargs='?',type=pathlib.Path,default=pathlib.Path('test-results'))
args=p.parse_args()
baseline=json.loads((args.directory/'baseline-tests.json').read_text())
native=json.loads((args.directory/'native-tests.json').read_text())
different={k:{'baseline':baseline.get(k),'native':native.get(k)} for k in baseline.keys()|native.keys() if baseline.get(k)!=native.get(k)}
print(len(baseline),'baseline tests;',len(native),'native tests;',len(different),'different outcomes')
if different:print(json.dumps(different,indent=2))
raise SystemExit(bool(different))
