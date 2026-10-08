"""Fail if the source package differs from the recorded port snapshot."""
import argparse
import hashlib
import json
import pathlib
import tomllib

p = argparse.ArgumentParser()
p.add_argument('source', type=pathlib.Path)
args = p.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
manifest = json.loads((root/'src/generated/manifest.json').read_text())
source = args.source/'src/fast_parse_time'
if not source.is_dir():
    source = args.source/'fast_parse_time'
actual = {p.relative_to(source).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
          for p in source.rglob('*.py')}
expected = manifest['source_sha256']
changed = sorted(p for p in expected.keys() | actual.keys() if expected.get(p) != actual.get(p))
version = tomllib.loads((args.source/'pyproject.toml').read_text())['project']['version']
if version != manifest['source_version']:
    changed.append('pyproject.toml: package version')
for path in changed:
    print('Source changed:', path)
print(len(actual), 'source files checked;', len(changed), 'differences')
raise SystemExit(bool(changed))
