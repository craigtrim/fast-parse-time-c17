# Frozen regression corpus

Captured from fast-parse-time 1.5.0 at source commit
4841982b791fd0c3a2da8b48e28c7c22e7d86a86, using the complete upstream tests.
Exact source hashes are in `src/generated/manifest.json`.

`observations.jsonl` contains 13,517 distinct legacy operation/input outcomes.
`calendar-observations.jsonl` contains 17,941 calendar operation/input/options
outcomes. Original ordering and exceptions are retained. The latter encodes
date, datetime, tuple, and bytes values with tagged JSON objects.
`context.json` records the source year/weekday and Python interpreter used;
the replay freezes those values so its results do not depend on today's date.

Run from the repository root after building the shared library:

```text
python tests/differential.py --corpus tests/data/observations.jsonl --calendar-corpus tests/data/calendar-observations.jsonl
```

The runner uses only Python's standard library and the native shared library.
No source package, pytest, word2number, or dateparser is required for replay.
The observations derive from the MIT-licensed upstream parser/tests.
