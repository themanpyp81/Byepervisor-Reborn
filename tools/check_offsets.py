#!/usr/bin/env python3
"""
Verify the per-firmware offset tables in include/offsets/.

The tables are positional arrays indexed by the ksym_t / kgadget_t enums in
include/kdlsym.h. The compiler can check that a table has the right *length*
(see the _Static_assert guards in kdlsym.c), but it cannot check the *order*
of the entries, because the order only exists in the trailing comments. A
table whose entries are out of order still compiles, still passes every bounds
check, and then reads the wrong kernel offsets at runtime.

This script checks the things the compiler cannot:

  1. every table lists its entries in the same order as the enums
  2. every table has exactly one entry per enum value
  3. every table's entries are non-zero
  4. every table in include/offsets/ is included by kdlsym.c
  5. every table in include/offsets/ is dispatched by the switches in kdlsym.c
  6. every version dispatched in kdlsym.c has a table

Points 4 and 6 are the exact failure that left the upstream repository
unbuildable: a commit added offset tables and the switch arms, but forgot the
matching #include lines in kdlsym.c.

Usage:  python3 tools/check_offsets.py [repo_root]
Exit code 0 on success, 1 if any check fails.
"""

import os
import re
import sys

FAIL = []
WARN = []


def fail(msg):
    FAIL.append(msg)


def warn(msg):
    WARN.append(msg)


def parse_enums(path):
    """Return {'sym': [...], 'gadget': [...]} from the ksym_t/kgadget_t enums."""
    src = open(path, encoding="utf-8").read()
    out = {"sym": [], "gadget": []}
    wanted = {"ksym_t": "sym", "kgadget_t": "gadget"}

    # finditer + non-greedy body keeps each typedef enum {...} NAME; block
    # separate; a plain search would let the first block swallow the second.
    for m in re.finditer(r"typedef\s+enum\s*\{(.*?)\}\s*(\w+)\s*;", src, re.S):
        body, enum_name = m.group(1), m.group(2)
        if enum_name not in wanted:
            continue
        names = []
        for line in body.split(","):
            name = line.split("//")[0].strip()
            if not name:
                continue
            if name.endswith("_MAX"):
                continue
            names.append(name)
        out[wanted[enum_name]] = names
    return out


def parse_tables(offsets_dir):
    """Return {basename: {'sym': [(value, label)], 'gadget': [...]}}."""
    tables = {}
    for fname in sorted(os.listdir(offsets_dir)):
        if not fname.endswith(".h"):
            continue
        path = os.path.join(offsets_dir, fname)
        src = open(path, encoding="utf-8").read()
        entry = {"sym": [], "gadget": []}

        for kind, array_re in (
            ("sym", r"g_sym_map_(\d+)\s*\[\s*\]\s*=\s*\{(.*?)\};"),
            ("gadget", r"g_gadget_map_(\d+)\s*\[\s*\]\s*=\s*\{(.*?)\};"),
        ):
            m = re.search(array_re, src, re.S)
            if not m:
                fail(f"{fname}: no {kind} map found")
                continue
            body = m.group(2)
            for line in body.split("\n"):
                line = line.strip()
                if not line:
                    continue
                vm = re.match(r"(0[xX][0-9a-fA-F]+)\s*,\s*//\s*(\S+)", line)
                if not vm:
                    # an entry without a symbol comment cannot be validated
                    fail(f"{fname}: {kind} entry has no symbol comment: {line!r}")
                    continue
                entry[kind].append((int(vm.group(1), 16), vm.group(2)))

        tables[fname[:-2]] = entry
    return tables


def check_table(name, table, enums):
    """Check one table against the expected enum ordering."""
    for kind in ("sym", "gadget"):
        expected = enums[kind]
        actual = [label for _, label in table[kind]]

        if not expected:
            continue

        if len(actual) != len(expected):
            fail(
                f"{name}: {kind} map has {len(actual)} entries, "
                f"expected {len(expected)} (one per enum value)"
            )

        for i, (exp, act) in enumerate(zip(expected, actual)):
            if exp != act:
                fail(
                    f"{name}: {kind} map entry {i} is {act}, "
                    f"expected {exp} -- tables are positional, "
                    f"a mismatch reads the wrong kernel offset"
                )

        for value, label in table[kind]:
            if value == 0:
                fail(f"{name}: {kind} entry {label} has offset 0x0")

        seen = {}
        for value, label in table[kind]:
            if value in seen:
                warn(
                    f"{name}: {kind} entries {seen[value]} and {label} "
                    f"share the offset 0x{value:X}"
                )
            else:
                seen[value] = label


def check_wiring(tables, kdlsym_path):
    """Check that every table is included and dispatched by kdlsym.c."""
    src = open(kdlsym_path, encoding="utf-8").read()

    includes = set(re.findall(r'#include\s+"offsets/([0-9_a-zA-Z]+)\.h"', src))
    dispatched = set(re.findall(r"g_(?:sym|gadget)_map_(\d+)\s*\[", src))
    # the dispatch switch uses the version as 0xNNNN0000
    versions = set(re.findall(r"case\s+(0x[0-9a-fA-F]+)\s*:", src))

    for name, table in tables.items():
        # table names look like 12_00 / 13_60; the array suffix is 1200 / 1360
        suffix = name.replace("_", "")
        if name not in includes:
            fail(
                f"{name}: table exists but kdlsym.c does not "
                f'#include "offsets/{name}.h" -- the symbols will be undeclared'
            )
        if suffix not in dispatched:
            fail(f"{name}: table exists but kdlsym.c never dispatches it")

    # every dispatched version needs a table
    known = {t.replace("_", "") for t in tables}
    for ver in sorted(versions):
        if not re.match(r"^0x1[23][0-9a-f]{6}$", ver):
            continue  # not a 12.xx / 13.xx version constant
        suffix = "%04X" % (int(ver, 16) >> 16)
        if suffix not in known:
            fail(
                f"kdlsym.c dispatches {ver} but include/offsets/ has no "
                f"matching table"
            )


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), ".."
    )
    root = os.path.abspath(root)

    kdlsym_h = os.path.join(root, "include", "kdlsym.h")
    offsets_dir = os.path.join(root, "include", "offsets")
    kdlsym_c = os.path.join(root, "kdlsym.c")

    for path in (kdlsym_h, offsets_dir, kdlsym_c):
        if not os.path.exists(path):
            print(f"error: {path} not found", file=sys.stderr)
            return 1

    enums = parse_enums(kdlsym_h)
    tables = parse_tables(offsets_dir)

    print(f"enum order from include/kdlsym.h:")
    print(f"  ksym_t     : {len(enums['sym'])} entries")
    print(f"  kgadget_t  : {len(enums['gadget'])} entries")
    print()

    for name, table in tables.items():
        check_table(name, table, enums)

    check_wiring(tables, kdlsym_c)

    print(f"checked {len(tables)} offset tables:")
    for name in sorted(tables):
        s = len(tables[name]["sym"])
        g = len(tables[name]["gadget"])
        status = "ok" if s == len(enums["sym"]) and g == len(enums["gadget"]) else "BAD"
        print(f"  {name:<7} {s:>2} syms, {g:>2} gadgets   {status}")
    print()

    for w in WARN:
        print(f"warning: {w}")
    if WARN:
        print()

    if FAIL:
        for f in FAIL:
            print(f"FAIL: {f}", file=sys.stderr)
        print(f"\n{len(FAIL)} check(s) failed", file=sys.stderr)
        return 1

    print("all offset table checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
