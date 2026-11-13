#!/usr/bin/env python3
"""Owned causal source variants; no native runtime binaries or device access."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1];out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
source=(root/'pc/stream-bridge-source-audit-windows.cpp').read_text()
needle='            if((module.error==ERROR_BAD_LENGTH||module.error==ERROR_PARTIAL_COPY||module.error==ERROR_MOD_NOT_FOUND)&&GetTickCount64()<deadline)return;'
assert source.count(needle)==1
retry=out/'no-retry';retry.mkdir(exist_ok=True);(retry/'loader.cpp').write_text(source.replace(needle,'            // Causal control: refuse after the first completed-load snapshot miss.'))
identity=out/'no-identity';identity.mkdir(exist_ok=True);(identity/'loader.cpp').write_text(source)
header=(root/'pc/stream-bridge-source-audit-modules-windows.h').read_text()
a=header.index('inline bool sameModuleFile(');b=header.index('\n// Public loader metadata',a)
header=header[:a]+'''inline bool sameModuleFile(HANDLE expected,const wchar_t* candidate)noexcept {
    (void)expected;(void)candidate;return false;
}
'''+header[b:]
(identity/'stream-bridge-source-audit-modules-windows.h').write_text(header)
