# RainRegExp 5.0.0.0

Native C++ / Win32 replacement for RainRegExp.au3. Copyright © 2026 Jeffrey
Morley. Licensed under CC BY-NC-SA 3.0 Unported; see COPYRIGHT.txt and
LICENSE.txt. PCRE keeps its original license.

## Run

Download the portable ZIP from [GitHub Releases](https://github.com/jsmorley/RainRegExp/releases), extract it, and run `RainRegExp.exe`. For a local build, run `dist/RainRegExp.exe`. Keep `RainRegExp.ini` beside it. No AutoIt runtime,
VC++ redistributable, external graphics, or separate PCRE DLL is required.
Windows 10 or later, x64. The original AutoIt program and files are untouched.

The original icon is embedded from `src/rX.ico`. The banner is embedded from
`src/RainRegExp.png`, a direct conversion of `src/RainRegExp.bmp`: white pixels
are transparent and all other original pixels are unchanged. No generated
artwork is used. General help and regex hints are embedded too.

## Connection settings

All settings are read from `[WebParser]` in the INI beside the executable
each time **Connect** is clicked. UTF-8 with or without BOM and UTF-16 LE/BE
with BOM are supported. ANSI files use the Windows system code page as a
fallback. Section names and keys are case insensitive. Missing or empty
ProxyServer/UserAgent values use their defaults.

```ini
[WebParser]
ProxyServer=/auto
UserAgent=Rainmeter WebParser plugin
Header=Accept: text/html
Header2=Accept-Language: en-US
Flags=Resync
```

- `ProxyServer=/auto`: use Windows Internet settings; `/none`: connect
  directly. An explicit server (for example `localhost:8080` or
  `http=localhost:8080;https=localhost:8080`) is passed to WinINet unchanged.
- `UserAgent`: the HTTP User-Agent string.
- `Header`, `Header2`, `Header3`, etc.: consecutive request headers. Like
  WebParser, reading stops at the first missing/empty header.
- `Flags`: case-insensitive names separated by `|`. Supported names:
  `ForceReload`, `Resync`, `NoCookies`, `Hyperlink`, `TempFile`, `NoAuth`,
  `NoCacheWrite`, `PragmaNoCache`, `Secure`, `IgnoreCertName`, `IgnoreCertDate`,
  `IgnoreHTTPRedirect`, `IgnoreHTTPSRedirect`.

`Resync` is always included, matching WebParser's default bitmask. Example:
`Flags=ForceReload|NoCookies|NoCacheWrite`. An invalid flag produces a visible
configuration error. Unlike Rainmeter, this utility stops that request rather
than logging and ignoring the bad flag. Comment lines start with `;`.

Connect uses WinINet and HTTP/HTTPS with 15-second connection/send/receive
timeouts. Browse loads local HTML/text files. Network responses are decoded
using UTF-8, WebParser's default CodePage. Rainmeter skin variables, formulas,
bangs and child measure processing are not evaluated by this standalone tool.

## Parse and other controls

Enter a URL and Connect, or Browse for a local file. Enter a pattern and click
Parse. Copy puts the expression on the Unicode clipboard. Search finds the
next case-insensitive occurrence in the HTML; after reaching the end it
beeps and resets, like the original. Wrap buttons preserve text and selection.
The Notepad tab is editable. The RegExp tab contains the original hints plus
WebParser compatibility notes.

**Regex matching uses Rainmeter's exact bundled PCRE 8.45 sources and
configuration**, pinned to revision
`54df7737c615ebe5a6b1d63e539aec171b54aec7`. See third_party/README.md for
provenance. No PCRE2 or std::regex substitution is used.

Like WebParser, the adapter compiles with flags `0`, executes once from offset
`0` with flags `0`, and uses a 300-integer capture vector. Output shows only
captured groups as **StringIndex 1–99**; index 0 is hidden. Intermediate unmatched
optional groups are empty; trailing unmatched groups are absent, as in
WebParser. Too many matched captures and PCRE execution errors are reported.
No extra match/depth limits or automatic UTF/UCP options are added. Explicit
pattern options, including `(?siU)` and `(*UCP)`, work as they do in Rainmeter.

The AutoIt code appends `.*$` and uses global matches. This application uses
the expression exactly as entered and performs a single match, to follow
WebParser as requested. For identical input text and pattern the PCRE call
and results match WebParser. Network decoding, a site's changing response,
or later Rainmeter postprocessing can still affect a skin's final value.

HTML Save writes UTF-8 with BOM. Session Save stores the URL, expression and
notes, matching the original session's scope; HTML/output are not stored and
Load does not fetch the URL. Legacy AutoIt `.txt` sessions can be loaded.
New sessions add an `EscapedV1` marker so multiline text and backslashes
round-trip without loss; new sessions are not intended for the AutoIt loader.
The last browse directory uses the original registry location.

## Build and tests

Install Visual Studio 2022 or newer with **Desktop development with C++**,
the Windows SDK and CMake tools. All PCRE sources are included; no dependency
downloads are required. Run from PowerShell:

```powershell
.\build.ps1
```

The script finds Visual Studio's CMake (or a CMake on PATH), configures an x64
Release build, runs CTest, then packages the EXE, INI and documentation into
`dist`. An existing `dist/RainRegExp.ini` is preserved.

For manual configuration with CMake on PATH:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Tests cover WebParser's literal PCRE calls, capture numbering/limits, optional
groups, inline flags, Unicode semantics, malformed patterns, sessions,
INI encodings, network defaults, numbered headers, and every supported flag.
Loopback integration tests also verify the actual HTTP User-Agent, custom
headers, explicit proxy routing, UTF-8 response decoding and parsing.

## Digital signing

The delivered EXE is unsigned. Self-signed certificates do not remove public
SmartScreen warnings. Public code signing can build publisher reputation,
but does not guarantee that a new download avoids warnings. No certificate
has been generated or installed and no Windows security settings are changed.

References:
- https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
- https://docs.rainmeter.net/manual/measures/webparser/
