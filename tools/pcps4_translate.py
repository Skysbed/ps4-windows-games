#!/usr/bin/env python3
"""
PcPs4 source translation layer.

The translator works at source level. It rewrites a defined subset of
common Win32/MSVC C/C++ constructs into PcPs4 compatibility calls and
reports APIs that need a real subsystem port.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import asdict, dataclass
from pathlib import Path


@dataclass
class Replacement:
    name: str
    pattern: str
    replacement: str
    count: int = 0


@dataclass
class Blocker:
    name: str
    line: int
    text: str


HEADER_RULES = [
    ("windows.h", r'#\s*include\s*[<"]windows\.h[>"]', '#include "pcps4_compat.h"'),
    ("synchapi.h", r'#\s*include\s*[<"]synchapi\.h[>"]', '#include "pcps4_compat.h"'),
]


TOKEN_RULES = [
    ("GetTickCount64", r'\bGetTickCount64\s*\(\s*\)', 'pcps4_monotonic_ms()'),
    ("GetTickCount", r'\bGetTickCount\s*\(\s*\)', '(uint32_t)pcps4_monotonic_ms()'),
    ("GetCurrentProcessId", r'\bGetCurrentProcessId\s*\(\s*\)', 'pcps4_get_current_process_id()'),
    ("Sleep", r'\bSleep\s*\(', 'pcps4_sleep_ms('),
    ("InterlockedIncrement", r'\bInterlockedIncrement\b', 'pcps4_interlocked_increment'),
    ("InterlockedDecrement", r'\bInterlockedDecrement\b', 'pcps4_interlocked_decrement'),

    ("_stricmp", r'\b_stricmp\b', 'strcasecmp'),
    ("_strnicmp", r'\b_strnicmp\b', 'strncasecmp'),
    ("stricmp", r'\bstricmp\b', 'strcasecmp'),
    ("strnicmp", r'\bstrnicmp\b', 'strncasecmp'),

    ("DWORD", r'\bDWORD\b', 'uint32_t'),
    ("WORD", r'\bWORD\b', 'uint16_t'),
    ("BYTE", r'\bBYTE\b', 'uint8_t'),
    ("UINT64", r'\bUINT64\b', 'uint64_t'),
    ("UINT", r'\bUINT\b', 'uint32_t'),
    ("ULONG", r'\bULONG\b', 'uint32_t'),
    ("LONG", r'\bLONG\b', 'int32_t'),
    ("ULONGLONG", r'\bULONGLONG\b', 'uint64_t'),
    ("LONGLONG", r'\bLONGLONG\b', 'int64_t'),
    ("HRESULT", r'\bHRESULT\b', 'int32_t'),
    ("BOOL", r'\bBOOL\b', 'int'),
    ("INT", r'\bINT\b', 'int32_t'),
    ("CHAR", r'\bCHAR\b', 'char'),
    ("WCHAR", r'\bWCHAR\b', 'uint16_t'),
    ("LPCSTR", r'\bLPCSTR\b', 'const char *'),
    ("LPSTR", r'\bLPSTR\b', 'char *'),
    ("LPVOID", r'\bLPVOID\b', 'void *'),
    ("PVOID", r'\bPVOID\b', 'void *'),
    ("SIZE_T", r'\bSIZE_T\b', 'size_t'),
    ("__int64", r'\b__int64\b', 'int64_t'),

    ("WINAPI", r'\bWINAPI\b', ''),
    ("__stdcall", r'\b__stdcall\b', ''),
    ("__cdecl", r'\b__cdecl\b', ''),
    ("__fastcall", r'\b__fastcall\b', ''),
    ("__declspec", r'__declspec\s*\(\s*(?:dllimport|dllexport|nothrow)\s*\)', ''),

    ("TRUE", r'\bTRUE\b', '1'),
    ("FALSE", r'\bFALSE\b', '0'),
]


BLOCKER_RULES = [
    ("DirectX header", r'#\s*include\s*[<"](?:d3d[0-9d_]*|dxgi|dxguid|d3dx)[^>"]*[>"]'),
    ("Winsock header", r'#\s*include\s*[<"](?:winsock2|ws2tcpip)\.h[>"]'),
    ("Win32 GUI API", r'\b(?:CreateWindow(?:Ex)?|RegisterClass(?:Ex)?|DefWindowProc|DispatchMessage|GetMessage|PeekMessage)\s*\('),
    ("DirectX API", r'\b(?:D3D11|D3D12|Direct3D|IDXGI|ID3D)\w*'),
    ("Windows socket API", r'\b(?:WSAStartup|WSACleanup|closesocket)\s*\('),
    ("Windows DLL loader", r'\b(?:LoadLibrary(?:A|W)?|FreeLibrary|GetProcAddress)\s*\('),
    ("Windows registry API", r'\b(?:RegOpenKey(?:Ex)?|RegCloseKey|RegQueryValue(?:Ex)?|RegSetValue(?:Ex)?)\s*\('),
    ("Win32 file API", r'\b(?:CreateFile(?:A|W)?|ReadFile|WriteFile|CloseHandle|GetFileSize)\s*\('),
    ("Win32 synchronization API", r'\b(?:CreateEvent(?:A|W)?|WaitForSingleObject|ReleaseMutex|CreateMutex(?:A|W)?)\s*\('),
]


def mask_non_code(source: str) -> str:
    out = list(source)
    i = 0
    n = len(source)

    while i < n:
        if source.startswith('//', i):
            j = source.find('\n', i)
            if j == -1:
                j = n
            for k in range(i, j):
                out[k] = ' '
            i = j
            continue

        if source.startswith('/*', i):
            j = source.find('*/', i + 2)
            j = n if j == -1 else j + 2
            for k in range(i, j):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
            continue

        if source[i] in ('"', "'"):
            quote = source[i]
            j = i + 1
            while j < n:
                if source[j] == '\\':
                    j += 2
                    continue
                if source[j] == quote:
                    j += 1
                    break
                j += 1
            for k in range(i, min(j, n)):
                if out[k] != '\n':
                    out[k] = ' '
            i = j
            continue

        i += 1

    return ''.join(out)


def apply_rules(source: str):
    rules = [Replacement(name, pattern, replacement) for name, pattern, replacement in HEADER_RULES]
    rules.extend(Replacement(name, pattern, replacement) for name, pattern, replacement in TOKEN_RULES)

    masked = mask_non_code(source)
    chunks: list[str] = []
    changed = False
    i = 0

    while i < len(source):
        is_code = masked[i] == source[i]
        j = i + 1
        while j < len(source) and (masked[j] == source[j]) == is_code:
            j += 1

        chunk = source[i:j]
        if is_code:
            for rule in rules:
                chunk, count = re.subn(rule.pattern, rule.replacement, chunk)
                rule.count += count
                changed |= count > 0

        chunks.append(chunk)
        i = j

    translated = ''.join(chunks)
    if changed and '#include "pcps4_compat.h"' not in translated:
        translated = '#include "pcps4_compat.h"\n' + translated

    return translated, rules, changed


def find_blockers(source: str):
    masked = mask_non_code(source)
    lines = source.splitlines()
    found: list[Blocker] = []

    for name, pattern in BLOCKER_RULES:
        for match in re.finditer(pattern, masked, re.IGNORECASE):
            line = masked.count('\n', 0, match.start()) + 1
            text = lines[line - 1].strip() if 1 <= line <= len(lines) else ''
            found.append(Blocker(name=name, line=line, text=text))

    return sorted(found, key=lambda item: (item.line, item.name))


def translate(source: str, strict: bool = False):
    translated, rules, changed = apply_rules(source)
    found = find_blockers(source)
    report = {
        "changed": changed,
        "replacements": [asdict(rule) for rule in rules if rule.count],
        "blockers": [asdict(item) for item in found],
    }
    status = 3 if strict and found else 0
    return translated, report, status


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Translate supported Windows/MSVC C/C++ constructs into PcPs4 compatibility calls.'
    )
    parser.add_argument('source', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--strict', action='store_true')
    args = parser.parse_args()

    try:
        source = args.source.read_text(encoding='utf-8')
    except OSError as exc:
        print(f'pcps4-translate: cannot read {args.source}: {exc}', file=sys.stderr)
        return 2

    translated, report, status = translate(source, strict=args.strict)

    try:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(translated, encoding='utf-8')
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    except OSError as exc:
        print(f'pcps4-translate: cannot write output: {exc}', file=sys.stderr)
        return 2

    print(f'PcPs4 translated: {args.source} -> {args.out}')
    for item in report["replacements"]:
        print(f'  {item["name"]}: {item["count"]}')
    for item in report["blockers"]:
        print(f'  UNSUPPORTED line {item["line"]}: {item["name"]}: {item["text"]}', file=sys.stderr)

    return status


if __name__ == '__main__':
    raise SystemExit(main())
