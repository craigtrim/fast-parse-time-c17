"""Replay recorded observations against the native library, including ordering."""
import argparse
import json
import pathlib
import datetime
from native_bridge import Native

p=argparse.ArgumentParser()
p.add_argument('--corpus',type=pathlib.Path,default=pathlib.Path('test-results/observations.jsonl'))
p.add_argument('--output',type=pathlib.Path,default=pathlib.Path('test-results/differential.json'))
p.add_argument('--calendar-corpus',type=pathlib.Path,default=pathlib.Path('test-results/calendar-observations.jsonl'))
args=p.parse_args()
metadata=args.corpus.parent/'context.json'
options=json.loads(metadata.read_text(encoding='utf-8')) if metadata.exists() else {}
native=Native(year=options.get('year',0),weekday=options.get('weekday',-1));failures=[];count=0
for line in args.corpus.read_text(encoding='utf-8').splitlines():
    row=json.loads(line);count+=1
    try:
        value=native.operation(row['op'],row['text'])
        actual={'value':value}
    except Exception as error:actual={'error':type(error).__name__}
    expected={k:v for k,v in row.items() if k not in ('text','op')}
    same=actual==expected
    if same and isinstance(actual.get('value'),dict):same=list(actual['value'].items())==list(expected['value'].items())
    if not same:failures.append({**row,'actual':actual})
if args.calendar_corpus.exists():
    def decode(value):
        if isinstance(value,dict):
            if '__date__' in value:return datetime.date.fromisoformat(value['__date__'])
            if '__datetime__' in value:return datetime.datetime.fromisoformat(value['__datetime__'])
            if '__tuple__' in value:return tuple(decode(x) for x in value['__tuple__'])
            if '__bytes__' in value:return bytes.fromhex(value['__bytes__'])
            return {k:decode(v) for k,v in value.items()}
        if isinstance(value,list):return [decode(x) for x in value]
        return value
    for line in args.calendar_corpus.read_text(encoding='utf-8').splitlines():
        row=json.loads(line);count+=1;options=decode(row['options'])
        try:actual={'value':native.calendar(decode(row['texts']),**options,extract=row['op']=='extract',raw=True)}
        except Exception as error:actual={'error':type(error).__name__}
        expected={k:v for k,v in row.items() if k not in ('op','texts','options')}
        if actual!=expected:failures.append({**row,'actual':actual})
native.close()
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps({'observations':count,'mismatches':len(failures),'failures':failures},indent=2),encoding='utf-8')
print(count,'observations;',len(failures),'mismatches')
for f in failures[:20]:print(json.dumps(f,ensure_ascii=True))
raise SystemExit(bool(failures))
