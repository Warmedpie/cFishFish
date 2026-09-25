#!/usr/bin/env python3
"""apply_tuned.py : paste tuner output into Eval.h.

    python3 tools/apply_tuned.py <tuner_output.txt> <Eval.h in> <Eval.h out>

Every "inline constexpr <Type> <Name> = ...;" declaration in the tuner output
replaces the declaration with the same name in Eval.h. The tuned piece-square
tables are in engine units, so the Stockfish-scale PST factors are set to 1:1.
"""
import re
import sys

tuned_path, src_path, dst_path = sys.argv[1:4]
tuned = open(tuned_path).read()
src = open(src_path).read()

decl = re.compile(r"inline constexpr (\w+) (\w+)(?:\[\d*\])?\s*= .*?;", re.S)
count = 0
for m in decl.finditer(tuned):
    name = m.group(2)
    # Match the same declaration in Eval.h (tables span lines up to "};").
    pat = re.compile(r"inline constexpr \w+ " + name + r"(?:\[\d*\])?\s*= .*?;", re.S)
    found = pat.search(src)
    if not found:
        sys.exit(f"declaration {name} not found in {src_path}")
    src = src[: found.start()] + m.group(0) + src[found.end():]
    count += 1

# PSTs are now in engine units: no Stockfish-scale factor.
for name in ("PstScaleNumMG", "PstScaleDenMG", "PstScaleNumEG", "PstScaleDenEG"):
    src, n = re.subn(r"(inline constexpr Score " + name + r" = )\d+;", r"\g<1>1;", src)
    if n != 1:
        sys.exit(f"{name} not found")

open(dst_path, "w").write(src)
print(f"replaced {count} declarations; PST scale set to 1:1")
