# RainRegExp 5.1

Native C++ / Win32 replacement for RainRegExp.au3. Copyright © 2026 Jeffrey
Morley. Licensed under CC BY-NC-SA 3.0 Unported; see COPYRIGHT.txt and
LICENSE.txt. PCRE keeps its original license.

## Run

Current release: **5.1**. Portable package: **RainRegExp_5.1.zip**.

Download the portable ZIP from [GitHub Releases](https://github.com/jsmorley/RainRegExp/releases), extract it, and run `RainRegExp.exe`. Keep `RainRegExp.ini` beside it.

## Interface

The native Win32 interface uses rounded panels and buttons, blue Connect and
Parse actions, underlined tabs, monospaced HTML/expression/output editors,
visible keyboard focus, wrap-toggle highlighting, and a capture count.
The app follows the Windows light/dark appearance and uses system colors in
high-contrast mode. The familiar panel arrangement and parsing, networking,
clipboard, file and session behavior are retained. It remains a portable EXE.

## Settings tab

The Settings tab edits the `RainRegExp.ini` beside the running executable.
Save validates the supported connection options, backs up the current file
as `RainRegExp.ini.bak`, and writes UTF-8 with BOM. Saved options take effect
on the next Connect without restarting. Reset puts the shipped defaults in
the editor; click Save to apply them. Exit returns to Welcome. Unsaved edits
are marked with an asterisk, and Exit/app close offer Save, Discard, or Cancel.
Switching to another tab preserves unsaved edits.
## Connection settings

All settings are read from `[WebParser]` in the INI beside the executable
each time **Connect** is clicked. UTF-8 with or without BOM and UTF-16 LE
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

HTML Save writes UTF-8 with BOM. Session Save stores the URL, expression and
notes, matching the original session's scope; HTML/output are not stored and
Load does not fetch the URL. Legacy AutoIt `.txt` sessions can be loaded.
New sessions add an `EscapedV1` marker so multiline text and backslashes
round-trip without loss; new sessions are not intended for the AutoIt loader.
The last browse directory uses the original registry location.
