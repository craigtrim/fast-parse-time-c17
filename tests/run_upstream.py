"""Run every upstream test, recording actual behavior or substituting the C port."""
import argparse
import functools
import datetime
import json
import pathlib
import sys
from oracle_loader import load

p = argparse.ArgumentParser()
p.add_argument('source', type=pathlib.Path)
p.add_argument('--mode', choices=['baseline', 'native'], default='baseline')
p.add_argument('--output', type=pathlib.Path, default=pathlib.Path('test-results'))
args = p.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
if args.mode=='baseline':
    (args.output/'context.json').write_text(json.dumps({'year':datetime.date.today().year,'weekday':datetime.date.today().weekday(),'python':sys.version},indent=2),encoding='utf-8')
api = load(args.source)
import pytest
import fast_parse_time.api as api_module
from fast_parse_time.explicit.bp import ExplicitTimeExtractor
from fast_parse_time.explicit.svc import TokenizeNumericComponents
from fast_parse_time.explicit.dmo.stdlib_date_validator import try_parse_date
from fast_parse_time.implicit.dmo import DigitTextReplacer

observations = {}
calendar_observations = {}
reports = {}

def calendar_record(name,function):
    def encode(value):
        if type(value) is datetime.date:return {'__date__':value.isoformat()}
        if type(value) is datetime.datetime:return {'__datetime__':value.isoformat()}
        if isinstance(value,tuple):return {'__tuple__':[encode(x) for x in value]}
        if isinstance(value,list):return [encode(x) for x in value]
        if isinstance(value,bytes):return {'__bytes__':value.hex()}
        if isinstance(value,dict):return {k:encode(v) for k,v in value.items()}
        return value
    @functools.wraps(function)
    def wrapped(texts,**options):
        record={'op':name,'texts':encode(texts),'options':encode(options)}
        try:
            key=json.dumps(record,sort_keys=True)
        except TypeError:
            return function(texts,**options)
        try:
            result=function(texts,**options)
        except Exception as error:
            calendar_observations[key]={**record,'error':type(error).__name__}
            raise
        value=result if name=='extract' else [{'text':x.text,'month':x.month,'day':x.day,'year':x.year,'date':x.date.isoformat() if x.date else None,'candidates':[d.isoformat() for d in x.candidates]} for x in result]
        calendar_observations[key]={**record,'value':value}
        return result
    return wrapped

def record(name, function):
    @functools.wraps(function)
    def wrapped(*a, **kw):
        text = a[-1] if a else kw.get('text', kw.get('input_text'))
        try:
            result = function(*a, **kw)
        except Exception as error:
            if isinstance(text, str):
                observations[(name, text)] = {'error': type(error).__name__}
            raise
        if isinstance(text, str):
            data = result
            if name == 'relative':
                data = [vars(v) for v in result]
            observations[(name, text)] = {'value': data}
        return result
    return wrapped

if args.mode == 'baseline':
    for public, name in [('extract_explicit_dates','explicit'), ('extract_relative_times','relative')]:
        func = record(name, getattr(api_module, public))
        setattr(api_module, public, func)
        setattr(api, public, func)
    numeric = record('numeric', api.extract_numeric_dates)
    api.extract_numeric_dates = numeric
    api_module.extract_numeric_dates = numeric
    for method in ['extract_numeric_dates','extract_written_dates','extract_hyphen_month_year','extract_prose_year','extract_iso8601_dates','extract_ordinal_dates','extract_space_month_number']:
        setattr(ExplicitTimeExtractor, method, record(method, getattr(ExplicitTimeExtractor, method)))
    TokenizeNumericComponents.process = record('tokenize', TokenizeNumericComponents.process)
    for public,name in [('extract_date_strings','extract'),('parse_date_strings','parse')]:
        fn=calendar_record(name,getattr(api_module,public));setattr(api_module,public,fn);setattr(api,public,fn)
else:
    from native_bridge import install
    install(api, api_module)

class Plugin:
    def pytest_runtest_logreport(self, report):
        if report.when == 'call' or report.failed:
            reports[report.nodeid] = {'outcome':report.outcome, 'xfail':getattr(report, 'wasxfail', None)}

exit_code = pytest.main([str(args.source.resolve()/'tests'), '-q', '--tb=short', '--import-mode=importlib', '-p', 'no:cacheprovider'], plugins=[Plugin()])
(args.output/(args.mode+'-tests.json')).write_text(json.dumps(reports, indent=2), encoding='utf-8')
if args.mode == 'baseline':
    with (args.output/'calendar-observations.jsonl').open('w',encoding='utf-8') as f:
        for row in calendar_observations.values():f.write(json.dumps(row,ensure_ascii=True)+'\n')
    with (args.output/'observations.jsonl').open('w', encoding='utf-8') as f:
        for (op,text), result in sorted(observations.items()):
            f.write(json.dumps({'op':op,'text':text, **result}, ensure_ascii=True)+'\n')
print('Recorded', len(reports), 'test outcomes;',len(observations),'legacy observations;',len(calendar_observations),'calendar observations')
sys.exit(exit_code)
