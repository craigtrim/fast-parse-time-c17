"""Exhaustive phrase-table parity plus generated, reproducible boundary cases."""
import argparse
import datetime
import json
import pathlib
import random
import runpy
import time
from oracle_loader import load
from native_bridge import Native

p=argparse.ArgumentParser()
p.add_argument('source',type=pathlib.Path)
p.add_argument('--output',type=pathlib.Path,default=pathlib.Path('test-results/extended.json'))
p.add_argument('--skip-kb',action='store_true')
args=p.parse_args()
api=load(args.source)
native=Native()
failures=[];counts={};start=time.monotonic()

def attempt(fn):
    try:return ('ok',fn())
    except Exception as e:return ('error',type(e).__name__)

def compare(kind,text,fn,native_fn=None):
    expected=attempt(fn)
    actual=attempt(native_fn or (lambda:native.operation(kind,text)))
    counts[kind]=counts.get(kind,0)+1
    same=actual==expected or (actual[0]==expected[0]=='error')
    if same and expected[0]=='ok' and isinstance(expected[1],dict):same=list(expected[1].items())==list(actual[1].items())
    if not same:
        failures.append({'kind':kind,'input':text,'expected':expected,'actual':actual})
        if len(failures)<=15:print(json.dumps(failures[-1],ensure_ascii=True),flush=True)

if not args.skip_kb:
    from fast_parse_time.implicit.dto.index_by_slot_kb import d_index_by_slot_kb as kb
    from fast_parse_time.implicit.dto.index_by_keyterm_kb import d_index_by_keyterm_kb
    from fast_parse_time.implicit.svc import AnalyzeTimeReferences
    from fast_parse_time.implicit.dmo import SequenceSolutionFinder
    analyzer=AnalyzeTimeReferences()
    # Only cache immutable index sets; preserve the upstream intersection,
    # ambiguity rule, normalizer, sequence extraction, filtering and compounds.
    cached_sets={key:set(value) for key,value in d_index_by_keyterm_kb.items()}
    def cached_find(sequences):
        result=[]
        for seq in sequences:
            sets=sorted((cached_sets[key] for key in seq),key=len)
            candidates=SequenceSolutionFinder._intersection(sets)
            if len(candidates)==1:result.append(kb[next(iter(candidates))])
            elif len(candidates)>1 and ' '.join(seq) in candidates:result.append(kb[' '.join(seq)])
        return result
    analyzer._find_solutions=cached_find
    for i,text in enumerate(kb):
        compare('relative',text,lambda text=text:[x._asdict() for x in analyzer.process(text)])
        if (i+1)%20000==0:print('KB',i+1,'/',len(kb),'mismatches',len(failures),flush=True)

rng=random.Random(7132026)
chars=['',' ','.',',','(',')','[',']',':','_','x','\x00','\u00a0','\u200b','\u0301','\u00b2','\u0660','\U00010400']
months=['Jan','February','Mar.','april','May','JUNE','July','Aug.','Sept','October','Nov','December','APRİL','ſeptember']
seps=['/','-','.',' - ','\u2013','\u2014',' / ','\t-\n']
numbers=['0','00','1','01','2','03','12','13','28','29','30','31','32','69','99','1925','1926','2000','2023','2024','2026','2036','2037','9999','+1','1_0','00001','\u0662','１２']
for i in range(15000):
    a,b,c=[rng.choice(numbers) for _ in range(3)];sep=rng.choice(seps)
    if i%4==0:text=f'{a}{sep}{b}{sep}{c}'
    elif i%4==1:text=f'{rng.choice(months)} {a}{rng.choice(["","st","nd","rd","th","TH"])}{rng.choice([" ",", ","-"])}{b}'
    elif i%4==2:text=f'{rng.choice(["in","since","from","between","on","the"])} {a} {rng.choice(["to","through","and","of"])} {rng.choice([b,rng.choice(months)])}'
    else:text=f'{a}{sep}{b}'
    text=rng.choice(chars)+text+rng.choice(chars)
    compare('explicit',text,lambda text=text:api.extract_explicit_dates(text))

units=['days','hour','hrs','min','years','week','month','sec','decades']
quantities=['1','01','0','999','1000','1001','2.9','1e2','1.e2','+2.5','-2.5','1_0.5','one','twenty-three','hundred','two-hundred-and-five','one-point-five','zero-point-zero','point','million-thousand','thousand-one','three--weeks','١٢','²','①']
for i in range(5000):
    text=' '.join([rng.choice(['','in','last','next','this','from']),rng.choice(quantities),rng.choice(units),rng.choice(['ago','back','before','prior','from now','before now','','AGO'])])
    if i%3==0:text+=f' and {rng.choice(quantities)} {rng.choice(units)} ago'
    compare('relative',text,lambda text=text:[vars(x) for x in api.extract_relative_times(text)])

# Number-word normalization, including shortest-roundtrip decimal strings.
from fast_parse_time.implicit.dmo import DigitTextReplacer
digit_replacer = DigitTextReplacer()
digit_words = ['zero','one','two','three','four','five','six','seven','eight','nine']
for i in range(5000):
    whole = rng.choice(['zero','one','two','nine','twenty','hundred','two-billion','one-trillion'])
    fractional = '-'.join(rng.choice(digit_words) for _ in range(rng.randrange(1, 24)))
    text = whole + '-point-' + fractional
    compare('replace', text, lambda text=text:digit_replacer.process(text.split()))

# Date arithmetic, including fractional microseconds and Gregorian boundaries.
references=[datetime.datetime(1,1,1),datetime.datetime(1900,3,1),datetime.datetime(2000,3,1,23,59,59,999999),datetime.datetime(2024,2,29,12,34,56,123456),datetime.datetime(9999,12,31)]
for frame in ['second','minute','hour','day','week','month','year','decade']:
    for card in [0,1,2,999,-1,0.5,1.5,0.0000005,0.0000015,0.0000025,1.0000005,2.0000015]:
        for tense in ['past','future','present']:
            rt=api.RelativeTime(card,frame,tense)
            label=f'{card} {frame} {tense}'
            compare('timedelta',label,lambda rt=rt:str(rt.to_timedelta()),lambda rt=rt:str(native.timedelta(rt)))
            for ref in references:
                compare('datetime',label+' '+ref.isoformat(),lambda rt=rt,ref=ref:rt.to_datetime(ref).isoformat(),lambda rt=rt,ref=ref:native.absolute(rt,ref).isoformat())

native.close()
args.output.parent.mkdir(parents=True,exist_ok=True)
args.output.write_text(json.dumps({'counts':counts,'mismatches':len(failures),'seconds':time.monotonic()-start,'failures':failures},indent=2),encoding='utf-8')
print(json.dumps({'counts':counts,'mismatches':len(failures),'seconds':time.monotonic()-start}),flush=True)
raise SystemExit(bool(failures))
