"""Regenerate immutable C tables from the pinned Python source (development only)."""
import argparse
import hashlib
import json
import pathlib
import runpy
import re
import sys
import tomllib
import _strptime
import unicodedata

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("source", type=pathlib.Path)
args = parser.parse_args()
src = args.source / "src" / "fast_parse_time"
if not src.is_dir():
    src = args.source / "fast_parse_time"
sys.path.insert(0,str(src.parent.resolve()))
out = ROOT / "src" / "generated"
out.mkdir(parents=True, exist_ok=True)

def c(s):
    return json.dumps(s, ensure_ascii=True)

kb_dir = src / 'implicit/knowledge_base'
if not kb_dir.is_dir():kb_dir = src / 'implicit/dto'
slots = runpy.run_path(str(kb_dir / 'index_by_slot_kb.py'))['d_index_by_slot_kb']
index = runpy.run_path(str(kb_dir / 'index_by_keyterm_kb.py'))['d_index_by_keyterm_kb']
terms = runpy.run_path(str(kb_dir / 'keyterm_counter_kb.py'))['d_keyterm_counter_kb']
index = {k: set(v) for k, v in index.items()}
rows = []
for key, slot in sorted(slots.items()):
    candidates = set.intersection(*(index[w] for w in key.split()))
    chosen = next(iter(candidates)) if len(candidates) == 1 else key if key in candidates else None
    if chosen is not None:
        v = slots[chosen]
        rows.append('{%s,{%s,%s,%s,%s}}' % (c(key), str(v.cardinality) if isinstance(v.cardinality, int) else '0', 'NULL' if isinstance(v.cardinality, int) else c(v.cardinality), c(v.frame), c(v.tense)))
    else:
        rows.append('{%s,{0,NULL,NULL,NULL}}' % c(key))
(out / 'kb.inc').write_text('/* Generated; do not edit. */\nstatic const kb_entry kb[] = {\n' + ',\n'.join(rows) + '\n};\nstatic const char *const keyterms[] = {\n' + ',\n'.join(c(k) for k in sorted(terms)) + '\n};\n', encoding='utf-8')

def ranges(pred):
    result = []
    for cp in range(0x110000):
        if pred(chr(cp)):
            if result and result[-1][1] == cp - 1:
                result[-1][1] = cp
            else:
                result.append([cp, cp])
    return result

properties = {name: ranges(fn) for name, fn in [('space', str.isspace), ('digit', str.isdigit), ('decimal', str.isdecimal), ('numeric', str.isnumeric), ('word', lambda s: s.isalnum() or s == '_')]}
u = ['/* Generated from Python Unicode %s. */' % unicodedata.unidata_version]
for name, rr in properties.items():
    u.append('static const urange u_%s[] = {%s};' % (name, ','.join('{%d,%d}' % tuple(r) for r in rr)))
lower = [(cp, chr(cp).lower()) for cp in range(0x110000) if chr(cp).lower() != chr(cp)]
u.append('static const ulower u_lower[] = {' + ','.join('{%d,{%s}}' % (cp, ','.join(str(ord(x)) for x in s)+',0') for cp,s in lower) + '};')
u.append('static const udigit u_digits[] = {' + ','.join('{%d,%d}' % (cp, unicodedata.decimal(chr(cp))) for cp in range(0x110000) if chr(cp).isdecimal()) + '};')
(out / 'unicode.inc').write_text('\n'.join(u)+'\n', encoding='utf-8')

def rxclass(rr):
    return '[' + ''.join('\\x{%x}' % a + ('-\\x{%x}' % b if a != b else '') for a,b in rr) + ']'
word = rxclass(properties['word'])
digit = rxclass(properties['decimal'])
space = rxclass(properties['space'])

def translate(p):
    # Python's word/whitespace definitions differ from PCRE2's UCP definitions.
    p = p.replace(r'\b', '(?:(?<!'+word+')(?='+word+')|(?<='+word+')(?!'+word+'))')
    p = p.replace(r'\d', digit).replace(r'\s', space)
    p = p.replace(r'[\w', '['+word[1:-1]).replace(r'\w', word)
    p = re.sub(r'\\u([0-9a-fA-F]{4})', lambda m: r'\x{'+m[1]+'}', p)
    # Python IGNORECASE includes both dotted and dotless I.
    if p.startswith('(?i)'):
        chunks = re.split(r'(\(\?P<[^>]+>)', p[4:])
        p = '(?i)' + ''.join(chunk if chunk.startswith('(?P<') else chunk.replace('i', '[iI\\x{130}\\x{131}]') for chunk in chunks)
    return p

month = 'september|february|november|december|january|october|august|march|april|june|july|sept|jan|feb|mar|apr|may|jun|jul|aug|sep|oct|nov|dec'
patterns = {
    'compact': r'(?i)\b(\d+(?:\.\d+)?)(mo|min|d|w|m|y|h|s)\b',
    'tense_suffix': r'^\s+(\b(ago|back|before)\b)',
    'spaced_hyphen': r'(\d)(?:\s+-\s*|\s*-\s+)(\d)',
    'strip_ordinal': r'(\d+)(st|nd|rd|th)\b',
    'written1': rf'(?i)({month})\.?\s+\d{{1,2}}(?:st|nd|rd|th)?,?\s+\d{{4}}',
    'written2': rf'(?i)\d{{1,2}}(?:st|nd|rd|th)?\s+({month})\.?,?\s+\d{{4}}',
    'hyphen_forward': rf'(?i)\b({month})\.?-(\d{{4}}|\d{{2}})\b',
    'hyphen_reverse': rf'(?i)\b(\d{{4}}|\d{{2}})-({month})\.?\b',
    'range_hyphen': r'\b(\d{4})-(\d{4})\b',
    'range_from': r'(?i)\bfrom\s+(\d{4})\s+(?:to|through)\s+(\d{4})\b',
    'range_between': r'(?i)\bbetween\s+(\d{4})\s+and\s+(\d{4})\b',
    'range_to': r'(?i)\b(\d{4})\s+to\s+(\d{4})\b',
    'range_abbrev': r'\b(\d{4})-(\d{2})\b',
    'prose_year': r'(?i)\b(?:as\s+of|back\s+to|prior\s+to|in|since|by|until|before|after|during|circa|around|from|through)\s+(\d{4})\b',
    'iso': r'\b(\d{4}-\d{2}-\d{2})T\d{2}:\d{2}:\d{2}(?:[.,]\d+)?(?:Z|[+-]\d{2}:\d{2})\b',
    'ordinal1': rf'(?i)\b(\d{{1,2}})(?:st|nd|rd|th)\s+day\s+of\s+({month})\.?(?:,?\s+(\d{{4}}))?',
    'ordinal2': rf'(?i)(?:the\s+)?(\d{{1,2}})(?:st|nd|rd|th)\s+of\s+({month})\.?(?:\s+(\d{{4}}))?\b',
    'ordinal3': rf'(?i)\b({month})\.?\s+(\d{{1,2}})(?:st|nd|rd|th)\b(?!\s*,?\s*\d{{4}})',
    'ordinal4': rf'(?i)\b(\d{{1,2}})(?:st|nd|rd|th)\s+({month})\.?\b(?!,?\s*\d{{4}})',
    'space_month': rf'(?i)(?:(in|on)\s+)?({month})\s+(\d{{2}})(?!\d)(?!(?:st|nd|rd|th))(?!,?\s*\d{{4}})',
    'day_token': r'\A\d{1,2}(st|nd|rd|th)?\Z',
}
validator_path=src/'explicit/validation.py'
if not validator_path.is_file():validator_path=src/'explicit/dmo/stdlib_date_validator.py'
validator = runpy.run_path(str(validator_path))
formats = validator['_FULL_YEAR_FORMATS'] + validator['_SHORT_YEAR_FORMATS']
for n, fmt in enumerate(formats):
    patterns['validate_%d' % n] = '(?i)\\A' + _strptime.TimeRE().pattern(fmt) + '\\z'
calendar_path=src/'explicit/calendar_parser.py'
if not calendar_path.is_file():calendar_path=src/'calendar_dates.py'
calendar = runpy.run_path(str(calendar_path))
for name, source_name in [('calendar_numeric','_NUMERIC'),('calendar_month_first','_MONTH_FIRST'),('calendar_day_first','_DAY_FIRST')]:
    pattern = calendar[source_name]
    patterns[name] = ('(?i)' if pattern.flags & re.IGNORECASE else '') + pattern.pattern
# No-year partial validation has a structural fallback and is implemented in C.
ids = ['/* Generated. */', 'enum {'] + ['RX_'+name.upper()+',' for name in patterns] + ['RX_COUNT};', '#define VALIDATION_FORMAT_COUNT %d' % len(formats)]
(out / 'regex_ids.h').write_text('\n'.join(ids)+'\n', encoding='utf-8')
rh = ['/* Generated; exact Python regex character classes. */', 'static const char *const regex_patterns[] = {']
rh += [c(translate(p).replace(r'\Z',r'\z'))+',' for p in patterns.values()]
rh += ['};']
(out / 'patterns.inc').write_text('\n'.join(rh)+'\n', encoding='utf-8')
git_dir=args.source/'.git'
head=(git_dir/'HEAD').read_text().strip()
if head.startswith('ref: '):
    ref=head[5:];ref_file=git_dir/ref
    head=ref_file.read_text().strip() if ref_file.is_file() else next(line.split()[0] for line in (git_dir/'packed-refs').read_text().splitlines() if line.endswith(' '+ref))
version=tomllib.loads((args.source/'pyproject.toml').read_text())['project']['version']
manifest = {'source_commit':head, 'source_version':version, 'source_state':'working tree; exact Python file hashes below', 'slot_count': len(slots), 'keyterm_count':len(terms), 'unicode_version':unicodedata.unidata_version, 'source_sha256': {p.relative_to(src).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(src.rglob('*.py'))}}
(out/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
header=ROOT/'include/fast_parse_time.h'
content=header.read_text()
content=re.sub(r'#define FPT_VERSION ".*"', '#define FPT_VERSION "'+version+'-c17.1"',content)
content=re.sub(r'#define FPT_SOURCE_COMMIT ".*"', '#define FPT_SOURCE_COMMIT "'+head+'"',content)
header.write_text(content,encoding='utf-8')
print('Generated', len(slots), 'slots,',len(terms),'keyterms,',len(patterns),'patterns')
