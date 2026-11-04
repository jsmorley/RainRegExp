PCRE sources are copied byte-for-byte from Rainmeter revision
54df7737c615ebe5a6b1d63e539aec171b54aec7, ThirdParty/pcre/.

Source: https://github.com/rainmeter/rainmeter/tree/54df7737c615ebe5a6b1d63e539aec171b54aec7/ThirdParty/pcre

All downloaded files were verified against that revision's Git blob hashes.
The complete configuration is preserved, including PCRE 8.45, SUPPORT_PCRE16,
SUPPORT_UTF, SUPPORT_UCP, NO_RECURSE, no JIT, default LF newline, and the
10,000,000 match and recursion limits. CMake compiles exactly the pcre16_*.c
translation units listed in Rainmeter's ThirdParty_PCRE.vcxproj.

The independent application adapter follows the calls in:

- https://github.com/rainmeter/rainmeter/blob/54df7737c615ebe5a6b1d63e539aec171b54aec7/Library/Pcre.h
- https://github.com/rainmeter/rainmeter/blob/54df7737c615ebe5a6b1d63e539aec171b54aec7/Library/MeasureWebParser.cpp

Compile flags=0, execution flags=0, start offset=0, extra=NULL, tables=NULL,
300 integer offsets. No wrapper source from Rainmeter is incorporated.
The PCRE library's original LICENCE and AUTHORS are included unchanged.
