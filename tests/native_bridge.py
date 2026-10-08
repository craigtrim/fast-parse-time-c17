"""ctypes test adapter. All extraction and date arithmetic execute in the C library."""
import ctypes as C
import datetime as dt
import os
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
class Explicit(C.Structure):
    _fields_=[('text',C.c_void_p),('length',C.c_size_t),('date_type',C.c_int)]
class Relative(C.Structure):
    _fields_=[('cardinality',C.c_double),('cardinality_text',C.c_char_p),('frame',C.c_char_p),('tense',C.c_char_p)]
class Text(C.Structure):
    _fields_=[('text',C.c_void_p),('length',C.c_size_t)]
class Result(C.Structure):
    _fields_=[('explicit',C.POINTER(Explicit)),('explicit_count',C.c_size_t),('relative',C.POINTER(Relative)),('relative_count',C.c_size_t),('tokens',C.POINTER(Text)),('token_count',C.c_size_t),('has_value',C.c_bool),('storage',C.c_void_p)]
class Options(C.Structure):
    _fields_=[('year',C.c_int),('weekday',C.c_int)]
class Delta(C.Structure):
    _fields_=[('days',C.c_int64),('seconds',C.c_int),('microseconds',C.c_int)]
class Date(C.Structure):
    _fields_=[(f,C.c_int) for f in ['year','month','day','hour','minute','second','microsecond']]
class Resolved(C.Structure):
    _fields_=[('parsed',Result),('datetimes',C.POINTER(Date)),('timedeltas',C.POINTER(Delta)),('count',C.c_size_t)]
class CalendarDate(C.Structure):
    _fields_=[('year',C.c_int),('month',C.c_int),('day',C.c_int)]
class CalendarOptions(C.Structure):
    _fields_=[('order',C.c_int),('has_range',C.c_bool),('start',CalendarDate),('end',CalendarDate)]
class Parsed(C.Structure):
    _fields_=[('text',C.c_void_p),('length',C.c_size_t),('month',C.c_int),('day',C.c_int),('year',C.c_int),('date',CalendarDate),('candidates',C.POINTER(CalendarDate)),('count',C.c_size_t)]
class CalendarResult(C.Structure):
    _fields_=[('dates',C.POINTER(Parsed)),('count',C.c_size_t),('storage',C.c_void_p)]

class Native:
    def __init__(self, library=None, year=0, weekday=-1):
        default = ROOT/'build'/('libfast_parse_time.dll' if os.name=='nt' else 'libfast_parse_time.so')
        self.lib=C.CDLL(str(library or os.environ.get('FPT_LIBRARY', default)))
        l=self.lib
        self.context=C.c_void_p()
        l.fpt_context_create.argtypes=[C.POINTER(Options),C.POINTER(C.c_void_p)]
        l.fpt_context_destroy.argtypes=[C.c_void_p]
        l.fpt_run.argtypes=[C.c_void_p,C.c_int,C.c_char_p,C.c_size_t,C.POINTER(Result)]
        l.fpt_result_free.argtypes=[C.POINTER(Result)]
        l.fpt_date_type_name.argtypes=[C.c_int];l.fpt_date_type_name.restype=C.c_char_p
        l.fpt_status_string.argtypes=[C.c_int];l.fpt_status_string.restype=C.c_char_p
        l.fpt_relative_to_timedelta.argtypes=[C.POINTER(Relative),C.POINTER(Delta)]
        l.fpt_relative_to_datetime.argtypes=[C.POINTER(Relative),C.POINTER(Date),C.POINTER(Date)]
        l.fpt_classify_delimited.argtypes=[C.c_void_p,C.c_char_p,C.c_size_t,C.c_char,C.POINTER(C.c_int)]
        l.fpt_parse_dates_with_type.argtypes=[C.c_void_p,C.c_char_p,C.c_size_t,C.c_char_p,C.POINTER(Result)]
        l.fpt_get_date_range.argtypes=[C.c_void_p,C.c_char_p,C.c_size_t,C.POINTER(Date),C.POINTER(Date),C.POINTER(Date),C.POINTER(C.c_bool)]
        for f in ['fpt_resolve_to_datetime','fpt_parse_and_resolve']:
            getattr(l,f).argtypes=[C.c_void_p,C.c_char_p,C.c_size_t,C.POINTER(Date),C.POINTER(Resolved)]
        l.fpt_resolve_to_timedelta.argtypes=[C.c_void_p,C.c_char_p,C.c_size_t,C.POINTER(Resolved)]
        l.fpt_resolved_result_free.argtypes=[C.POINTER(Resolved)]
        l.fpt_parse_date_strings.argtypes=[C.c_void_p,C.POINTER(Text),C.c_size_t,C.POINTER(CalendarOptions),C.POINTER(CalendarResult)]
        l.fpt_extract_date_strings.argtypes=[C.c_void_p,C.POINTER(Text),C.c_size_t,C.POINTER(CalendarResult)]
        l.fpt_calendar_result_free.argtypes=[C.POINTER(CalendarResult)]
        self.check(l.fpt_context_create(C.byref(Options(year,weekday)),C.byref(self.context)))

    def check(self,status):
        if status:
            cls={2:MemoryError,3:UnicodeError,5:OverflowError,6:NotImplementedError,7:TypeError,8:AttributeError}.get(status,ValueError)
            raise cls(self.lib.fpt_status_string(status).decode())

    def close(self):
        if self.context:
            self.lib.fpt_context_destroy(self.context);self.context=None

    def convert(self,r):
        explicit={C.string_at(x.text,x.length).decode():self.lib.fpt_date_type_name(x.date_type).decode() for x in r.explicit[:r.explicit_count]}
        relative=[{'cardinality':x.cardinality_text.decode() if x.cardinality_text else int(x.cardinality) if x.cardinality.is_integer() else x.cardinality,'frame':x.frame.decode(),'tense':x.tense.decode()} for x in r.relative[:r.relative_count]]
        tokens=[C.string_at(x.text,x.length).decode() for x in r.tokens[:r.token_count]]
        return explicit,relative,tokens,bool(r.has_value)

    def run(self,op,text):
        if not isinstance(text,str):
            if op==1:return {},[],[],False
            raise TypeError('expected str')
        b=text.encode();r=Result()
        self.check(self.lib.fpt_run(self.context,op,b,len(b),C.byref(r)))
        try:return self.convert(r)
        finally:self.lib.fpt_result_free(C.byref(r))

    def operation(self,name,text):
        ops={'explicit':1,'relative':2,'numeric':3,'extract_numeric_dates':3,'extract_written_dates':4,'extract_hyphen_month_year':5,'extract_prose_year':6,'extract_iso8601_dates':7,'extract_ordinal_dates':8,'extract_space_month_number':9,'normalize':10,'tokenize':11,'preclassify':12,'validate':13,'replace':14,'compact':15}
        e,r,t,h=self.run(ops[name],text)
        if name=='relative':return r
        if name in ['tokenize','replace']:return t or (None if name=='tokenize' else [])
        if name in ['normalize','compact']:return t[0]
        if name in ['validate','preclassify']:return h
        return e if name=='explicit' else e or None

    @staticmethod
    def rel(value):
        card=value.cardinality
        return Relative(0 if isinstance(card,str) else card,card.encode() if isinstance(card,str) else None,value.frame.encode(),value.tense.encode())

    @staticmethod
    def date(value):
        return Date(*(getattr(value,f) for f,_ in Date._fields_)) if value is not None else None

    @staticmethod
    def datetime(value,reference=None):
        fields={f:getattr(value,f) for f,_ in Date._fields_}
        return reference.replace(**fields,fold=0) if reference is not None else dt.datetime(**fields)

    def timedelta(self,value):
        out=Delta();self.check(self.lib.fpt_relative_to_timedelta(C.byref(self.rel(value)),C.byref(out)))
        return dt.timedelta(days=out.days,seconds=out.seconds,microseconds=out.microseconds)

    def absolute(self,value,reference=None):
        out=Date();ref=self.date(reference)
        self.check(self.lib.fpt_relative_to_datetime(C.byref(self.rel(value)),C.byref(ref) if ref else None,C.byref(out)))
        return self.datetime(out,reference)

    def resolved(self,text,reference=None,kind='datetime'):
        b=text.encode();ref=self.date(reference);r=Resolved()
        args=[self.context,b,len(b)]
        if kind!='timedelta':args.append(C.byref(ref) if ref else None)
        args.append(C.byref(r))
        function={'datetime':'fpt_resolve_to_datetime','timedelta':'fpt_resolve_to_timedelta','all':'fpt_parse_and_resolve'}[kind]
        self.check(getattr(self.lib,function)(*args))
        try:
            if kind=='timedelta':return [dt.timedelta(days=x.days,seconds=x.seconds,microseconds=x.microseconds) for x in r.timedeltas[:r.count]]
            dates=[self.datetime(x,reference) for x in r.datetimes[:r.count]]
            return {'explicit':list(self.convert(r.parsed)[0]),'resolved':dates} if kind=='all' else dates
        finally:self.lib.fpt_resolved_result_free(C.byref(r))

    def calendar(self,texts,date_range=None,date_order='auto',extract=False,raw=False):
        if date_order not in ('auto','mdy','dmy'):raise ValueError('invalid date_order')
        opt=CalendarOptions(('auto','mdy','dmy').index(date_order),date_range is not None)
        if date_range is not None:
            if not isinstance(date_range,tuple) or len(date_range)!=2 or any(type(x) is not dt.date for x in date_range):raise TypeError('invalid date_range type')
            opt.start=CalendarDate(date_range[0].year,date_range[0].month,date_range[0].day)
            opt.end=CalendarDate(date_range[1].year,date_range[1].month,date_range[1].day)
        if isinstance(texts,str):texts=[texts]
        if not isinstance(texts,list) or any(not isinstance(x,str) for x in texts):raise TypeError('expected string or list of strings')
        encoded=[x.encode() for x in texts]
        array=(Text*len(texts))(*(Text(C.cast(C.c_char_p(x),C.c_void_p),len(x)) for x in encoded))
        r=CalendarResult()
        status=self.lib.fpt_extract_date_strings(self.context,array,len(texts),C.byref(r)) if extract else self.lib.fpt_parse_date_strings(self.context,array,len(texts),C.byref(opt),C.byref(r))
        self.check(status)
        try:
            if extract:return [C.string_at(x.text,x.length).decode() for x in r.dates[:r.count]]
            if raw:return [{'text':C.string_at(x.text,x.length).decode(),'month':x.month or None,'day':x.day or None,'year':x.year or None,'date':dt.date(x.date.year,x.date.month,x.date.day).isoformat() if x.date.year else None,'candidates':[dt.date(v.year,v.month,v.day).isoformat() for v in x.candidates[:x.count]]} for x in r.dates[:r.count]]
            from fast_parse_time import ParsedDate
            return [ParsedDate(C.string_at(x.text,x.length).decode(),x.month or None,x.day or None,tuple(dt.date(v.year,v.month,v.day) for v in x.candidates[:x.count])) for x in r.dates[:r.count]]
        finally:self.lib.fpt_calendar_result_free(C.byref(r))

def install(api,module):
    native=Native()
    def relative(text):return [api.RelativeTime(**x) for x in native.run(2,text)[1]]
    def explicit(text):return native.run(1,text)[0]
    def parsed(text):
        e,r,_,_=native.run(0,text)
        return api.ParseResult([api.ExplicitDate(k,v) for k,v in e.items()],[api.RelativeTime(**x) for x in r])
    def filtered(text,date_type=None):
        b=text.encode();r=Result();native.check(native.lib.fpt_parse_dates_with_type(native.context,b,len(b),date_type.encode() if isinstance(date_type,str) else None if date_type is None else b'unknown',C.byref(r)))
        try:return native.convert(r)[0]
        finally:native.lib.fpt_result_free(C.byref(r))
    def date_range(text):
        b=text.encode();a=Date();z=Date();found=C.c_bool()
        native.check(native.lib.fpt_get_date_range(native.context,b,len(b),None,C.byref(a),C.byref(z),C.byref(found)))
        return (native.datetime(a),native.datetime(z)) if found.value else None
    funcs={'parse_dates':parsed,'extract_explicit_dates':explicit,'extract_relative_times':relative,'parse_time_references':relative,'extract_numeric_dates':lambda text=None,input_text=None:native.operation('numeric',input_text if input_text is not None else text),'parse_dates_with_type':filtered,'extract_ambiguous_dates':lambda text:filtered(text,'DAY_MONTH_AMBIGUOUS'),'extract_full_dates_only':lambda text:filtered(text,'FULL_EXPLICIT_DATE'),'has_temporal_info':lambda text:parsed(text).has_dates,'extract_past_references':lambda text:[x for x in relative(text) if x.tense=='past'],'extract_future_references':lambda text:[x for x in relative(text) if x.tense=='future'],'resolve_to_datetime':lambda text,reference=None:native.resolved(text,reference),'resolve_to_timedelta':lambda text:native.resolved(text,kind='timedelta'),'parse_and_resolve':lambda text,reference=None:native.resolved(text,reference,'all'),'get_date_range':date_range}
    for name,fn in funcs.items():setattr(api,name,fn);setattr(module,name,fn)
    api.extract_date_strings=module.extract_date_strings=lambda texts:native.calendar(texts,extract=True)
    api.parse_date_strings=module.parse_date_strings=lambda texts,*,date_range=None,date_order='auto':native.calendar(texts,date_range,date_order)
    api.RelativeTime.to_timedelta=lambda self:native.timedelta(self)
    api.RelativeTime.to_datetime=lambda self,reference=None:native.absolute(self,reference)
    from fast_parse_time.explicit.bp import ExplicitTimeExtractor
    for name in ['extract_numeric_dates','extract_written_dates','extract_hyphen_month_year','extract_prose_year','extract_iso8601_dates','extract_ordinal_dates','extract_space_month_number']:
        def method(self,input_text,op=name):return native.operation(op,input_text)
        setattr(ExplicitTimeExtractor,name,method)
    from fast_parse_time.explicit.svc import TokenizeNumericComponents
    TokenizeNumericComponents.process=lambda self,input_text:native.operation('tokenize',input_text)
    from fast_parse_time.implicit.svc import AnalyzeTimeReferences
    from fast_parse_time.implicit.dto.index_by_slot_kb import Slot
    AnalyzeTimeReferences.process=lambda self,input_text:[Slot(**x) for x in native.operation('relative',input_text)]
    from fast_parse_time.implicit.dmo import DigitTextReplacer
    DigitTextReplacer.process=lambda self,tokens:native.operation('replace',' '.join(tokens))
    import fast_parse_time.explicit.dmo.stdlib_date_validator as validator
    validator.try_parse_date=lambda text:bool(text) and native.operation('validate',text)
    try:
        import fast_parse_time.explicit.validation as canonical_validator
        canonical_validator.try_parse_date=validator.try_parse_date
    except ImportError:
        pass
    from fast_parse_time.explicit.dmo import DelimitedDateClassifier
    def classify(self,input_text,delimiter):
        b=input_text.encode();out=C.c_int()
        native.check(native.lib.fpt_classify_delimited(native.context,b,len(b),delimiter.encode(),C.byref(out)))
        return api.DateType(out.value) if out.value else None
    DelimitedDateClassifier.process=classify
    return native
