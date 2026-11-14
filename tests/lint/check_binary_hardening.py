#!/usr/bin/env python3
"""H2: check that a Linux server binary carries the exploit-mitigation build flags.

Reads the ELF file directly (no binutils), so it needs only Python. Checks, for each binary:
position-independent executable, RELRO with immediate binding, a non-executable stack, stack
protector use and (with --fortify) _FORTIFY_SOURCE checked libc calls. Binaries that are not
64-bit little-endian ELF are skipped, so the same test runs on every platform.

  check_binary_hardening.py [--fortify] BINARY...
  check_binary_hardening.py --selftest
"""
import argparse
import re
import struct
import sys

PT_INTERP, PT_DYNAMIC, PT_GNU_STACK, PT_GNU_RELRO = 3, 2, 0x6474E551, 0x6474E552
DT_NULL, DT_BIND_NOW, DT_FLAGS, DT_FLAGS_1 = 0, 24, 30, 0x6FFFFFFB
DF_BIND_NOW, DF_1_NOW, DF_1_PIE = 0x8, 0x1, 0x08000000
SHT_SYMTAB, SHT_DYNSYM = 2, 11
ET_DYN = 3
PF_X = 1


class NotElf(Exception):
    pass


def parse(data):
    """Returns (type, program headers [(type, flags, offset, filesz)], dynamic {tag: value}, symbols)."""
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise NotElf
    e_type = struct.unpack_from("<H", data, 16)[0]
    e_phoff, e_shoff = struct.unpack_from("<QQ", data, 32)
    e_phentsize, e_phnum, e_shentsize, e_shnum = struct.unpack_from("<HHHH", data, 54)
    phdrs = []
    for i in range(e_phnum):
        p_type, p_flags, p_offset, _, _, p_filesz = struct.unpack_from(
            "<IIQQQQ", data, e_phoff + i * e_phentsize)
        phdrs.append((p_type, p_flags, p_offset, p_filesz))
    dynamic = {}
    for p_type, _, offset, size in phdrs:
        if p_type != PT_DYNAMIC:
            continue
        for pos in range(offset, offset + size, 16):
            tag, value = struct.unpack_from("<qQ", data, pos)
            if tag == DT_NULL:
                break
            dynamic[tag] = dynamic.get(tag, 0) | value if tag in (DT_FLAGS, DT_FLAGS_1) else value
    sections = []
    for i in range(e_shnum if e_shoff else 0):
        sections.append(struct.unpack_from("<IIQQQQII", data, e_shoff + i * e_shentsize))
    symbols = set()
    for _, s_type, _, _, offset, size, link, _ in sections:
        if s_type not in (SHT_SYMTAB, SHT_DYNSYM):
            continue
        str_offset, str_size = sections[link][4], sections[link][5]
        strings = data[str_offset:str_offset + str_size]
        for pos in range(offset, offset + size, 24):
            name = struct.unpack_from("<I", data, pos)[0]
            end = strings.find(b"\0", name)
            if name and end > name:
                symbols.add(strings[name:end].decode("ascii", "replace"))
    return e_type, phdrs, dynamic, symbols


def problems(data, fortify):
    e_type, phdrs, dynamic, symbols = parse(data)
    found = []
    if e_type != ET_DYN:
        found.append("not a position-independent executable (ELF type is not DYN)")
    if not any(p[0] == PT_GNU_RELRO for p in phdrs):
        found.append("no RELRO segment")
    bind_now = (DT_BIND_NOW in dynamic or dynamic.get(DT_FLAGS, 0) & DF_BIND_NOW
                or dynamic.get(DT_FLAGS_1, 0) & DF_1_NOW)
    if not bind_now:
        found.append("lazy binding (no BIND_NOW), so the GOT stays writable")
    stacks = [p for p in phdrs if p[0] == PT_GNU_STACK]
    if not stacks or stacks[0][1] & PF_X:
        found.append("stack is executable or has no GNU_STACK header")
    if "__stack_chk_fail" not in symbols:
        found.append("no stack protector (__stack_chk_fail is never referenced)")
    if fortify and not any(re.fullmatch(r"__\w+_chk", s) and s != "__stack_chk_fail"
                           for s in symbols):
        found.append("no _FORTIFY_SOURCE checked calls (no __*_chk imports)")
    return found


def elf(e_type=ET_DYN, relro=True, bind_now=True, exec_stack=False, symbols=()):
    """Builds a minimal ELF64 for the self-test."""
    phdr = []
    dyn = struct.pack("<qQ", DT_FLAGS_1, DF_1_NOW if bind_now else 0) + struct.pack("<qQ", 0, 0)
    strtab = b"\0" + b"".join(s.encode() + b"\0" for s in symbols)
    symtab = b"\0" * 24
    pos = 1
    for s in symbols:
        symtab += struct.pack("<IBBHQQ", pos, 0x10, 0, 0, 0, 0)
        pos += len(s) + 1
    phnum = 2 + (1 if relro else 0)
    base = 64 + 56 * phnum
    phdr.append(struct.pack("<IIQQQQQQ", PT_DYNAMIC, 6, base, 0, 0, len(dyn), len(dyn), 8))
    phdr.append(struct.pack("<IIQQQQQQ", PT_GNU_STACK, 7 if exec_stack else 6, 0, 0, 0, 0, 0, 16))
    if relro:
        phdr.append(struct.pack("<IIQQQQQQ", PT_GNU_RELRO, 4, 0, 0, 0, 0, 0, 1))
    str_off = base + len(dyn)
    sym_off = str_off + len(strtab)
    sh_off = sym_off + len(symtab)
    body = b"".join(phdr) + dyn + strtab + symtab
    sections = [b"\0" * 64,
                struct.pack("<IIQQQQIIQQ", 0, 3, 0, 0, str_off, len(strtab), 0, 0, 1, 0),
                struct.pack("<IIQQQQIIQQ", 0, SHT_DYNSYM, 0, 0, sym_off, len(symtab), 1, 0, 8, 24)]
    header = (b"\x7fELF\x02\x01\x01" + b"\0" * 9
              + struct.pack("<HHIQQQIHHHHHH", e_type, 62, 1, 0, 64, sh_off, 0, 64, 56, phnum, 64,
                            len(sections), 0))
    return header + body + b"".join(sections)


def selftest():
    good = elf(symbols=["__stack_chk_fail", "__memcpy_chk", "puts"])
    assert problems(good, True) == [], problems(good, True)
    assert problems(elf(e_type=2, symbols=["__stack_chk_fail"]), False)[0].startswith("not a position")
    cases = {
        "RELRO": elf(relro=False, symbols=["__stack_chk_fail"]),
        "BIND_NOW": elf(bind_now=False, symbols=["__stack_chk_fail"]),
        "executable": elf(exec_stack=True, symbols=["__stack_chk_fail"]),
        "stack protector": elf(symbols=["puts"]),
    }
    for needle, data in cases.items():
        assert any(needle in p for p in problems(data, False)), (needle, problems(data, False))
    assert any("FORTIFY" in p for p in problems(elf(symbols=["__stack_chk_fail"]), True))
    assert not any("FORTIFY" in p for p in problems(elf(symbols=["__stack_chk_fail"]), False))
    try:
        parse(b"MZ" + b"\0" * 100)
    except NotElf:
        pass
    else:
        raise AssertionError("a non-ELF file must be rejected")
    print("binary_hardening selftest=ok")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fortify", action="store_true")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("binaries", nargs="*")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    status = 0
    for path in args.binaries:
        with open(path, "rb") as handle:
            data = handle.read()
        try:
            found = problems(data, args.fortify)
        except NotElf:
            print(f"{path}: skipped (not a 64-bit little-endian ELF file)")
            continue
        for message in found:
            print(f"{path}: {message}")
        if found:
            status = 1
        else:
            print(f"{path}: hardened")
    return status


if __name__ == "__main__":
    sys.exit(main())
