# Third-party notices

## fast-parse-time

The parser behavior, phrase knowledge base, and generated regression observations
derive from Craig Trim's fast-parse-time. The original MIT license is preserved
in LICENSE. The source snapshot is identified by file hashes in
src/generated/manifest.json, including the 1.5.0 package reorganization.

## PCRE2 10.45

The complete upstream C source is vendored in vendor/pcre2-10.45. Its BSD license
and Unicode data notices are in vendor/pcre2-10.45/LICENCE.md. It is statically linked
into the executable and shared library; building requires no network access.

Source: https://github.com/PCRE2Project/pcre2/releases/tag/pcre2-10.45

Archive SHA-256:
0e138387df7835d7403b8351e2226c1377da804e0737db0e071b48f07c9d12ee

## word2number 1.1

The C number-word normalization reproduces the algorithm in word2number 1.1.
There is no runtime Python dependency.

Copyright (c) 2016 Akshay Nagpal (https://github.com/akshaynagpal)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

## Python Unicode and date-format tables

Generated Unicode 14.0.0 character properties, lower-case mappings, and strptime
format patterns were produced with CPython 3.11. Python is used only by table
generation and verification tools. CPython is distributed under the PSF license;
see vendor/PYTHON-LICENSE.txt. PCRE2's own Unicode tables retain their upstream
license notices. The generated character classes preserve Python's whitespace,
digit, word-boundary, and case-insensitive matching rules.
