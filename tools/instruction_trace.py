#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded Linux x86-64 instruction tracing for the differential harness.

The tracer single-steps only declared guest/replacement executable ranges. It uses Capstone's
operand access metadata to record supported explicit memory operands and models selected implicit
stack effects. Known unsupported string, vector-memory, system, atomic, and special-memory forms
are rejected before execution. Calls into uninstrumented host code are stepped over with a
temporary return breakpoint and make coverage incomplete.
"""
from __future__ import annotations

import ctypes
import ctypes.util
import os
from pathlib import Path
import signal
import struct
import sys


PTRACE_TRACEME, PTRACE_PEEKDATA, PTRACE_POKEDATA = 0, 2, 5
PTRACE_CONT, PTRACE_KILL, PTRACE_SINGLESTEP = 7, 8, 9
PTRACE_GETREGS, PTRACE_SETREGS = 12, 13
CS_ARCH_X86, CS_MODE_64, CS_OPT_DETAIL, CS_OPT_ON = 3, 1 << 3, 2, 3
CS_OP_IMM, CS_OP_MEM, CS_AC_READ, CS_AC_WRITE = 2, 3, 1, 2
CS_GRP_JUMP, CS_GRP_CALL, CS_GRP_RET = 1, 2, 3
WORD = ctypes.sizeof(ctypes.c_long)
MAX_EVENTS = 4096
MAX_GAPS = 256
MAX_CFG_STEPS = 100_000
STRING_MEMORY_MNEMONICS = {
    "movsb", "movsw", "movsq", "stosb", "stosw", "stosd", "stosq",
    "lodsb", "lodsw", "lodsd", "lodsq", "scasb", "scasw", "scasd", "scasq",
    "cmpsb", "cmpsw", "cmpsd", "cmpsq", "insb", "insw", "insd",
    "outsb", "outsw", "outsd",
}
SYSTEM_INSTRUCTIONS = {
    "syscall", "sysret", "sysretq", "sysenter", "sysexit", "int", "int1", "int3",
    "into", "iret", "iretd", "iretq", "lcall", "ljmp", "retf", "retfq", "rsm",
    "hlt", "cli", "sti", "clts", "invd", "wbinvd", "swapgs", "rdmsr", "wrmsr",
    "rdpmc", "rdtsc", "rdtscp", "in", "out", "monitor", "mwait", "umonitor",
    "umwait", "tpause", "vmcall", "vmmcall", "vmrun", "vmlaunch", "vmresume",
    "vmexit", "vmclear", "vmxon", "vmxoff", "vmptrld", "vmptrst", "vmread", "vmwrite",
    "vmfunc", "vmload", "vmsave", "invept", "invvpid", "clgi", "stgi", "skinit",
    "xsetbv", "encls", "enclu", "enclv", "xbegin", "xend", "xabort", "xtest",
    "enqcmd", "enqcmds", "tdcall", "seamcall", "seamret", "rdpkru", "wrpkru",
    "rdfsbase", "wrfsbase", "rdgsbase", "wrgsbase", "incsspd", "incsspq",
    "rstorssp", "saveprevssp", "setssbsy", "clrssbsy", "wrss", "wrssd", "wrssq",
    "wrussd", "wrussq",
}
NONDETERMINISTIC_INSTRUCTIONS = {"rdrand", "rdseed"}
STATE_MEMORY_INSTRUCTIONS = {
    "enter", "xlat", "xlatb", "bound", "lgdt", "lidt", "sgdt", "sidt", "lldt",
    "ltr", "sldt", "str", "verr", "verw", "smsw", "lmsw", "invlpg", "invpcid",
    "clflush", "clflushopt", "clwb", "clzero", "fxsave", "fxsave64", "fxrstor",
    "fxrstor64", "xsave", "xsave64", "xsaveopt", "xsaveopt64", "xsavec", "xsavec64",
    "xsaves", "xsaves64", "xrstor", "xrstor64", "xrstors", "xrstors64", "fnsave",
    "fnsave64", "frstor", "frstor64", "fnstenv", "fstenv", "fldenv", "ldmxcsr",
    "stmxcsr", "maskmovq", "maskmovdqu", "vmaskmovdqu", "movdir64b", "cldemote",
}


class Registers(ctypes.Structure):
    _fields_ = [(name, ctypes.c_ulonglong) for name in (
        "r15 r14 r13 r12 rbp rbx r11 r10 r9 r8 rax rcx rdx rsi rdi orig_rax "
        "rip cs eflags rsp ss fs_base gs_base ds es fs gs").split()]


class IOVec(ctypes.Structure):
    _fields_ = [("base", ctypes.c_void_p), ("length", ctypes.c_size_t)]


class X86Memory(ctypes.Structure):
    _fields_ = [("segment", ctypes.c_uint), ("base", ctypes.c_uint),
                ("index", ctypes.c_uint), ("scale", ctypes.c_int),
                ("disp", ctypes.c_int64)]


class X86OperandValue(ctypes.Union):
    _fields_ = [("reg", ctypes.c_uint), ("imm", ctypes.c_int64), ("mem", X86Memory)]


class X86Operand(ctypes.Structure):
    _fields_ = [("type", ctypes.c_uint), ("value", X86OperandValue),
                ("size", ctypes.c_uint8), ("access", ctypes.c_uint8),
                ("broadcast", ctypes.c_int), ("zero_mask", ctypes.c_bool)]


class X86(ctypes.Structure):
    _fields_ = [("prefix", ctypes.c_uint8 * 4), ("opcode", ctypes.c_uint8 * 4),
                ("rex", ctypes.c_uint8), ("addr_size", ctypes.c_uint8),
                ("modrm", ctypes.c_uint8), ("sib", ctypes.c_uint8),
                ("disp", ctypes.c_int64), ("sib_index", ctypes.c_uint),
                ("sib_scale", ctypes.c_int8), ("_pad", ctypes.c_uint8 * 3),
                ("sib_base", ctypes.c_uint), ("xop_cc", ctypes.c_int),
                ("sse_cc", ctypes.c_int), ("avx_cc", ctypes.c_int),
                ("avx_sae", ctypes.c_bool), ("_pad2", ctypes.c_uint8 * 3),
                ("avx_rm", ctypes.c_int), ("flags", ctypes.c_uint64),
                ("op_count", ctypes.c_uint8), ("_pad3", ctypes.c_uint8 * 7),
                ("operands", X86Operand * 8), ("encoding", ctypes.c_uint8 * 5)]


class DetailUnion(ctypes.Union):
    _fields_ = [("x86", X86), ("_align", ctypes.c_uint64)]


class Detail(ctypes.Structure):
    _fields_ = [("regs_read", ctypes.c_uint16 * 20), ("regs_read_count", ctypes.c_uint8),
                ("_pad", ctypes.c_uint8), ("regs_write", ctypes.c_uint16 * 20),
                ("regs_write_count", ctypes.c_uint8), ("groups", ctypes.c_uint8 * 8),
                ("groups_count", ctypes.c_uint8), ("writeback", ctypes.c_bool),
                ("_pad2", ctypes.c_uint8 * 3), ("arch", DetailUnion)]


class Instruction(ctypes.Structure):
    _fields_ = [("id", ctypes.c_uint), ("address", ctypes.c_uint64),
                ("size", ctypes.c_uint16), ("bytes", ctypes.c_uint8 * 24),
                ("mnemonic", ctypes.c_char * 32), ("op_str", ctypes.c_char * 160),
                ("detail", ctypes.POINTER(Detail))]


class Capstone:
    def __init__(self):
        path = None
        try:
            import capstone
            bundled = Path(capstone.__file__).parent / "lib" / "libcapstone.so"
            if bundled.is_file():
                path = str(bundled)
        except ImportError:
            pass
        path = path or ctypes.util.find_library("capstone")
        if not path:
            raise TraceUnavailable("libcapstone is unavailable; install the Capstone development package")
        self.lib = ctypes.CDLL(path)
        self.lib.cs_version.argtypes = [ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
        major, minor = ctypes.c_int(), ctypes.c_int()
        self.lib.cs_version(ctypes.byref(major), ctypes.byref(minor))
        if major.value != 5:
            raise TraceUnavailable(f"Capstone 5 is required for the supported detail ABI; found {major.value}.{minor.value}")
        self.lib.cs_open.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_size_t)]
        self.lib.cs_open.restype = ctypes.c_int
        self.lib.cs_option.argtypes = [ctypes.c_size_t, ctypes.c_int, ctypes.c_size_t]
        self.lib.cs_option.restype = ctypes.c_int
        self.lib.cs_disasm.argtypes = [ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t,
                                       ctypes.c_uint64, ctypes.c_size_t,
                                       ctypes.POINTER(ctypes.POINTER(Instruction))]
        self.lib.cs_disasm.restype = ctypes.c_size_t
        self.lib.cs_free.argtypes = [ctypes.POINTER(Instruction), ctypes.c_size_t]
        self.lib.cs_reg_name.argtypes = [ctypes.c_size_t, ctypes.c_uint]
        self.lib.cs_reg_name.restype = ctypes.c_char_p
        self.handle = ctypes.c_size_t()
        if self.lib.cs_open(CS_ARCH_X86, CS_MODE_64, ctypes.byref(self.handle)):
            raise TraceUnavailable("Capstone could not initialize x86-64 decoder")
        if self.lib.cs_option(self.handle, CS_OPT_DETAIL, CS_OPT_ON):
            raise TraceUnavailable("Capstone detail mode is unavailable")

    def decode(self, address: int, code: bytes) -> dict:
        if not code:
            raise TraceUnavailable(f"no executable bytes at 0x{address:x}")
        source = ctypes.create_string_buffer(code)
        output = ctypes.POINTER(Instruction)()
        count = self.lib.cs_disasm(self.handle, source, len(code), address, 1, ctypes.byref(output))
        if count != 1:
            raise TraceUnavailable(f"Capstone could not decode instruction at 0x{address:x}")
        try:
            insn = output.contents
            instruction_detail = insn.detail.contents
            detail = instruction_detail.arch.x86
            operands = []
            operand_sizes = []
            branch_target = None
            for operand in detail.operands[:detail.op_count]:
                operand_sizes.append(operand.size)
                if operand.type == CS_OP_IMM and branch_target is None:
                    branch_target = int(operand.value.imm) & ((1 << 64) - 1)
                if operand.type != CS_OP_MEM:
                    continue
                mem = operand.value.mem
                name = lambda ident: (self.lib.cs_reg_name(self.handle, ident) or b"").decode()
                operands.append({"base": name(mem.base), "index": name(mem.index),
                                 "segment": name(mem.segment), "scale": mem.scale,
                                 "disp": mem.disp, "size": operand.size, "access": operand.access})
            raw = bytes(insn.bytes[:insn.size])
            groups = instruction_detail.groups[:instruction_detail.groups_count]
            if CS_GRP_RET in groups:
                flow_kind = "return"
            elif CS_GRP_CALL in groups:
                flow_kind = "call"
            elif CS_GRP_JUMP in groups:
                flow_kind = "jump" if insn.mnemonic.decode().lower() in ("jmp", "jmpq") else "conditional"
            else:
                flow_kind = None
            return {"size": insn.size, "mnemonic": insn.mnemonic.decode(),
                    "text": insn.op_str.decode(), "operands": operands, "bytes": raw.hex(),
                    "vex_or_evex": raw[:1] in (b"\xc4", b"\xc5", b"\x62"),
                    "address_size": detail.addr_size,
                    "prefixes": [value for value in detail.prefix if value],
                    "operand_sizes": operand_sizes, "control_flow": flow_kind,
                    "branch_target": branch_target if flow_kind else None}
        finally:
            self.lib.cs_free(output, count)


class TraceUnavailable(RuntimeError):
    pass


def _stable_location(pc: int, identity: str, ranges: list[tuple[int, int, str]]) -> str | None:
    """Use stable guest or module-relative addresses instead of tracee ASLR addresses."""
    if not any(lo <= pc < hi and label == identity for lo, hi, label in ranges):
        return None
    if identity == "original":
        return f"guest:0x{pc - 0x800000000 + 0x400000:x}"
    base = min(lo for lo, _, label in ranges if label == identity)
    return f"{identity}:+0x{pc - base:x}"


def _control_flow_coverage(steps: list[tuple], ranges: list[tuple[int, int, str]],
                           trace_complete: bool, stop_reason: str | None,
                           collection_truncated: bool, flow_error: str | None) -> dict:
    """Summarize dynamically observed blocks and edges; incomplete data is never zero coverage."""
    if not steps:
        return {"schema": 1, "status": "unavailable", "feedback_eligible": False,
                "reason": "no guest instructions were observed", "blocks": [], "edges": []}

    leaders: set[tuple[str, int]] = {(steps[0][0], steps[0][1])}
    for identity, _pc, next_pc, flow_kind, branch_target, _outcome, _size in steps:
        if not flow_kind:
            continue
        if branch_target is not None:
            target_identity = next((label for lo, hi, label in ranges if lo <= branch_target < hi), None)
            if target_identity:
                leaders.add((target_identity, branch_target))
        next_identity = next((label for lo, hi, label in ranges if lo <= next_pc < hi), None)
        if next_identity:
            leaders.add((next_identity, next_pc))

    block_hits: dict[str, int] = {}
    edge_hits: dict[tuple[str, str, str, str, str], int] = {}
    current_block: str | None = None
    previous_identity: str | None = None
    previous_flow = False
    for identity, pc, next_pc, flow_kind, _branch_target, outcome, _size in steps:
        location = _stable_location(pc, identity, ranges)
        if location is None:
            continue
        if current_block is None or previous_identity != identity or previous_flow or (identity, pc) in leaders:
            current_block = location
            block_hits[location] = block_hits.get(location, 0) + 1
        if flow_kind:
            target_identity = next((label for lo, hi, label in ranges if lo <= next_pc < hi), None)
            destination = _stable_location(next_pc, target_identity, ranges) if target_identity else "exit"
            edge = (current_block, destination or "unavailable", flow_kind, outcome or "unknown", location)
            edge_hits[edge] = edge_hits.get(edge, 0) + 1
        previous_identity = identity
        previous_flow = bool(flow_kind)

    blocks = [{"address": address, "hits": hits} for address, hits in sorted(block_hits.items())]
    edges = [{"from": source, "to": target, "kind": kind, "outcome": outcome,
              "branch_instruction": instruction, "hits": hits}
             for (source, target, kind, outcome, instruction), hits in sorted(edge_hits.items())]
    complete = not collection_truncated and not flow_error and stop_reason is None
    reasons = []
    if collection_truncated:
        reasons.append(f"CFG step limit {MAX_CFG_STEPS} reached")
    if flow_error:
        reasons.append(flow_error)
    if stop_reason:
        reasons.append(f"instruction trace stopped: {stop_reason}")
    feedback_eligible = complete and trace_complete
    if complete and not trace_complete:
        reasons.append("instruction trace was incomplete; observed CFG is not used for fixture feedback")
    return {"schema": 1, "status": "complete" if complete else "partial",
            "feedback_eligible": feedback_eligible, "reason": "; ".join(reasons) or None,
            "blocks": blocks, "edges": edges}


def _instruction_support_issue(insn: dict) -> tuple[str, str] | None:
    """Return the capability/reason for semantics this tracer deliberately rejects."""
    words = insn["mnemonic"].lower().split()
    mnemonic = words[-1] if words else ""
    operands = insn["operands"]
    prefixes = set(insn.get("prefixes", ()))
    repeat = bool(prefixes.intersection((0xF2, 0xF3))) or any(
        word in ("rep", "repe", "repz", "repne", "repnz") for word in words[:-1])

    string_operation = mnemonic in STRING_MEMORY_MNEMONICS
    if mnemonic in ("movsd", "cmpsd"):
        # These spellings also name scalar SSE instructions. The string encodings have
        # implicit RSI/RDI memory operands and no XMM operands in Capstone's text.
        string_operation = "xmm" not in insn["text"].lower()
    if string_operation:
        if repeat:
            return "repeat_string_memory", f"REP/REPE/REPNE string operation {insn['mnemonic']} is not traced"
        return "string_memory", f"implicit string memory operation {insn['mnemonic']} is not traced"

    if insn["vex_or_evex"] and operands:
        return "vex_evex_memory", "VEX/EVEX memory access semantics are not traced"
    if 0xF0 in prefixes and operands:
        return "atomic_memory", "LOCK-prefixed memory operation has unmodeled atomic semantics"
    if mnemonic == "xchg" and operands:
        return "atomic_memory", "memory XCHG has implicit atomic semantics"
    if mnemonic in SYSTEM_INSTRUCTIONS:
        return "system_instruction", f"system/control instruction {insn['mnemonic']} has opaque effects"
    if mnemonic in NONDETERMINISTIC_INSTRUCTIONS:
        return "nondeterministic_instruction", f"hardware-random instruction {insn['mnemonic']} is not replayable"
    if mnemonic in ("cmpxchg8b", "cmpxchg16b"):
        return "atomic_memory", f"{insn['mnemonic']} has implicit atomic semantics"
    if mnemonic in STATE_MEMORY_INSTRUCTIONS or mnemonic.startswith(("prefetch", "xsave", "xrstor")):
        return "special_memory_operation", f"special memory operation {insn['mnemonic']} has unmodeled access semantics"
    segment_operand = insn["text"].strip().lower()
    if mnemonic in ("push", "pop") and segment_operand in {"cs", "ds", "es", "fs", "gs", "ss"}:
        return "stack_segment_state", (f"segment-register {segment_operand} {insn['mnemonic']} "
                                        "has unmodeled stack width or state effects")
    if mnemonic in ("call", "ret") and 0x66 in prefixes:
        return "stack_width", f"operand-size override on {insn['mnemonic']} is not modeled"

    for index, operand in enumerate(operands):
        if operand["access"] not in (CS_AC_READ, CS_AC_WRITE, CS_AC_READ | CS_AC_WRITE):
            return "memory_access_classification", f"decoder did not classify memory operand {index}"
        if operand["size"] <= 0 or operand["size"] > 64:
            return "memory_access_width", f"memory operand {index} width {operand['size']} is unsupported"
    return None


def _ptrace(libc, request: int, pid: int, address: int = 0, data: int = 0) -> int:
    ctypes.set_errno(0)
    result = libc.ptrace(request, pid, ctypes.c_void_p(address), ctypes.c_void_p(data))
    if result == -1 and ctypes.get_errno():
        raise OSError(ctypes.get_errno(), os.strerror(ctypes.get_errno()))
    return int(result)


def _regs(libc, pid: int) -> Registers:
    value = Registers()
    _ptrace(libc, PTRACE_GETREGS, pid, 0, ctypes.addressof(value))
    return value


def _setregs(libc, pid: int, regs: Registers) -> None:
    _ptrace(libc, PTRACE_SETREGS, pid, 0, ctypes.addressof(regs))


def _peek(libc, pid: int, address: int) -> int:
    return _ptrace(libc, PTRACE_PEEKDATA, pid, address, 0) & ((1 << (WORD * 8)) - 1)


def _poke(libc, pid: int, address: int, value: int) -> None:
    _ptrace(libc, PTRACE_POKEDATA, pid, address, value)


def _read_memory(libc, pid: int, address: int, size: int) -> bytes | None:
    if size <= 0 or size > 64:
        return None
    local = ctypes.create_string_buffer(size)
    local_iov, remote_iov = IOVec(ctypes.addressof(local), size), IOVec(address, size)
    try:
        got = libc.process_vm_readv(pid, ctypes.byref(local_iov), 1,
                                    ctypes.byref(remote_iov), 1, 0)
    except AttributeError:
        return None
    return local.raw[:got] if got == size else None


def _maps(pid: int) -> list[tuple[int, int, str, str]]:
    result = []
    with open(f"/proc/{pid}/maps", encoding="ascii") as stream:
        for line in stream:
            fields = line.split(maxsplit=5)
            lo, hi = (int(value, 16) for value in fields[0].split("-"))
            result.append((lo, hi, fields[1], fields[5].strip() if len(fields) > 5 else ""))
    return result


def _exec_ranges(boot: dict, pid: int, replacement: str | None) -> tuple[list[tuple[int, int, str]], list[dict]]:
    ranges = [(0x800000000 + vaddr, 0x800000000 + vaddr + size, "original")
              for vaddr, size, flags in boot["loads"] if flags & 1]
    regions = [{"start": 0x800000000 + vaddr, "end": 0x800000000 + vaddr + size,
                "name": "guest_image", "read": bool(flags & 4), "write": bool(flags & 2)}
               for vaddr, size, flags in boot["loads"]]
    if replacement:
        expected = os.path.realpath(replacement)
        for lo, hi, perms, path in _maps(pid):
            if path == expected:
                if "x" in perms:
                    ranges.append((lo, hi, "replacement"))
                if "r" in perms or "w" in perms:
                    regions.append({"start": lo, "end": hi, "name": "replacement_module",
                                    "read": "r" in perms, "write": "w" in perms})
    for lo, hi, perms, path in _maps(pid):
        if path == "[stack]":
            regions.append({"start": lo, "end": hi, "name": "host_stack", "read": True, "write": True})
    return ranges, regions


def _register(regs: Registers, name: str) -> int | None:
    if name in ("", "invalid", "riz", "eiz"):
        return 0
    aliases = {"eax": ("rax", 32), "ax": ("rax", 16), "al": ("rax", 8), "ah": ("rax", 8),
               "ebx": ("rbx", 32), "bx": ("rbx", 16), "bl": ("rbx", 8), "bh": ("rbx", 8),
               "ecx": ("rcx", 32), "cx": ("rcx", 16), "cl": ("rcx", 8), "ch": ("rcx", 8),
               "edx": ("rdx", 32), "dx": ("rdx", 16), "dl": ("rdx", 8), "dh": ("rdx", 8),
               "esi": ("rsi", 32), "si": ("rsi", 16), "sil": ("rsi", 8),
               "edi": ("rdi", 32), "di": ("rdi", 16), "dil": ("rdi", 8),
               "esp": ("rsp", 32), "sp": ("rsp", 16), "spl": ("rsp", 8),
               "ebp": ("rbp", 32), "bp": ("rbp", 16), "bpl": ("rbp", 8),
               "eip": ("rip", 32), "ip": ("rip", 16)}
    if name in aliases:
        field, bits = aliases[name]
        value = getattr(regs, field)
        return value & ((1 << bits) - 1)
    if name.endswith("d") and name[:-1] in {f"r{i}" for i in range(8, 16)}:
        return getattr(regs, name[:-1]) & 0xffffffff
    if name.endswith("w") and name[:-1] in {f"r{i}" for i in range(8, 16)}:
        return getattr(regs, name[:-1]) & 0xffff
    if name.endswith("b") and name[:-1] in {f"r{i}" for i in range(8, 16)}:
        return getattr(regs, name[:-1]) & 0xff
    field_names = {field[0] for field in Registers._fields_}
    return getattr(regs, name) if name in field_names and name not in {"orig_rax", "cs", "eflags", "ss", "fs", "gs", "ds", "es"} else None


def _effective_address(op: dict, regs: Registers, insn_pc: int, insn_size: int,
                       address_size: int = 8, rsp_increment: int = 0) -> tuple[int | None, list[str]]:
    gaps = []
    address = op["disp"]
    for field, factor in (("base", 1), ("index", op["scale"])):
        name = op[field]
        if name in ("", "invalid", "riz", "eiz"):
            continue
        value = _register(regs, name)
        if value is None:
            gaps.append(f"unsupported address register {name}")
            return None, gaps
        if name in ("rip", "eip"):
            value = insn_pc + insn_size
        elif name in ("rsp", "esp"):
            # POP r/m computes an RSP-relative destination EA after popping its source.
            # Other instructions use the pre-instruction value supplied in `regs`.
            value += rsp_increment
        address += value * factor
    address &= (1 << (address_size * 8)) - 1
    segment = op["segment"]
    if segment in ("fs", "gs"):
        address += getattr(regs, segment + "_base")
    elif segment not in ("", "invalid", "ds", "es", "ss", "cs"):
        gaps.append(f"unsupported address segment {segment}")
        return None, gaps
    address &= (1 << 64) - 1
    return address, gaps


def _region(address: int, size: int, write: bool, regions: list[dict]) -> tuple[str, bool]:
    if size <= 0 or address + size > 1 << 64:
        return "invalid", False
    for item in regions:
        if item["start"] <= address and address + size <= item["end"]:
            return item["name"], bool(item["write"] if write else item["read"])
    return "unmapped_or_undeclared", False


def _event(address: int, width: int, read: bool, write: bool, regs: Registers,
           pc: int, identity: str, regions: list[dict], libc, pid: int,
           instruction: dict, operand: int | None) -> dict:
    region, valid = _region(address, width, write, regions)
    if read and write:
        valid = valid and _region(address, width, False, regions)[1]
    before = _read_memory(libc, pid, address, width)
    item = {"identity": identity, "instruction": f"0x{pc:x}",
            "mnemonic": instruction["mnemonic"], "operand": operand,
            "access": "read_write" if read and write else "write" if write else "read",
            "address": f"0x{address:x}", "width": width, "region": region,
            "valid_region": valid, "before": before.hex() if before is not None else None,
            "after": None,
            "state_before": {name: f"0x{getattr(regs, name):x}" for name in
                             ("rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", "r8", "r9", "r10", "r11", "rip")}}
    if identity == "original":
        item["instruction_offset"] = f"0x{pc - 0x800000000 + 0x400000:x}"
    for region_info in regions:
        if region_info["start"] <= address < region_info["end"]:
            item["region_offset"] = f"+0x{address - region_info['start']:x}"
            break
    if "region_offset" not in item:
        item["region_offset"] = f"0x{address:x}"
    return item


def _breakpoint(libc, pid: int, address: int) -> tuple[int, int]:
    word_address = address & ~(WORD - 1)
    word = _peek(libc, pid, word_address)
    shift = (address - word_address) * 8
    patched = (word & ~(0xff << shift)) | (0xcc << shift)
    _poke(libc, pid, word_address, patched)
    return word, shift


def _restore_breakpoint(libc, pid: int, address: int, saved: tuple[int, int]) -> None:
    word_address = address & ~(WORD - 1)
    current = _peek(libc, pid, word_address)
    word, shift = saved
    original_byte = (word >> shift) & 0xff
    _poke(libc, pid, word_address, (current & ~(0xff << shift)) | (original_byte << shift))


def trace_child(pid: int, target: int, boot: dict, case: dict,
                replacement: str | None = None, instruction_limit: int = 100_000) -> dict:
    """Trace a TRACEME child, returning per-instruction memory events and explicit gaps."""
    libc = ctypes.CDLL(None, use_errno=True)
    libc.ptrace.restype = ctypes.c_long
    libc.ptrace.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p]
    libc.process_vm_readv.argtypes = [ctypes.c_int, ctypes.POINTER(IOVec), ctypes.c_ulong,
                                      ctypes.POINTER(IOVec), ctypes.c_ulong, ctypes.c_ulong]
    libc.process_vm_readv.restype = ctypes.c_ssize_t
    decoder = Capstone()
    _, status = os.waitpid(pid, 0)
    if not os.WIFSTOPPED(status):
        raise TraceUnavailable("tracee did not stop after PTRACE_TRACEME")
    ranges, regions = _exec_ranges(boot, pid, replacement)
    buffer_offset = 0
    for name, spec in sorted(case.get("buffers", {}).items()):
        size = int(spec["size"])
        start = 0x900000000 + buffer_offset
        regions.append({"start": start, "end": start + size, "name": f"buf:{name}",
                        "read": True, "write": True})
        pages = max(1, (size + 4095) // 4096)
        buffer_offset += (pages + 1) * 4096
    result = {"schema": 1, "complete": True, "instruction_limit": instruction_limit,
              "instructions": 0, "events": [], "gaps": [], "skipped_host_calls": 0}
    cfg_steps: list[tuple] = []
    cfg_collection_truncated = False
    cfg_error = None
    entry_sp = None
    breakpoint_saved = _breakpoint(libc, pid, target)
    _ptrace(libc, PTRACE_CONT, pid)
    _, status = os.waitpid(pid, 0)
    if not os.WIFSTOPPED(status) or os.WSTOPSIG(status) != signal.SIGTRAP:
        _ptrace(libc, PTRACE_KILL, pid)
        raise TraceUnavailable("tracee did not reach the function-entry breakpoint")
    _restore_breakpoint(libc, pid, target, breakpoint_saved)
    regs = _regs(libc, pid)
    regs.rip = target
    entry_sp = regs.rsp
    _setregs(libc, pid, regs)
    gaps_seen = set()

    active_mnemonic = "<outside_instrumented_range>"

    def mark_gap(pc: int, reason: str, mnemonic: str | None = None,
                 capability: str = "instruction_semantics") -> None:
        result["complete"] = False
        gap = (pc, mnemonic or active_mnemonic, capability, reason)
        if gap in gaps_seen:
            return
        if len(gaps_seen) < MAX_GAPS:
            gaps_seen.add(gap)
        else:
            result["gaps_truncated"] = True

    while True:
        regs = _regs(libc, pid)
        pc = regs.rip
        active_mnemonic = "<outside_instrumented_range>"
        identity = next((label for lo, hi, label in ranges if lo <= pc < hi), None)
        if identity is None:
            # Skip foreign host code by breaking at its return address if it returns into a
            # declared range. Otherwise let the tracee finish (normal return to Python).
            try:
                return_pc = _read_memory(libc, pid, regs.rsp, 8)
                return_pc = struct.unpack("<Q", return_pc)[0] if return_pc else 0
                if any(lo <= return_pc < hi for lo, hi, _ in ranges):
                    saved = _breakpoint(libc, pid, return_pc)
                    result["skipped_host_calls"] += 1
                    mark_gap(pc, "host call body is outside instrumented executable ranges",
                             "host_call", "execution_scope")
                    _ptrace(libc, PTRACE_CONT, pid)
                    _, status = os.waitpid(pid, 0)
                    if os.WIFSTOPPED(status) and os.WSTOPSIG(status) == signal.SIGTRAP:
                        _restore_breakpoint(libc, pid, return_pc, saved)
                        regs = _regs(libc, pid)
                        if regs.rip != return_pc + 1:
                            result["complete"] = False
                            result["stop_reason"] = "unexpected_SIGTRAP_while_skipping_host_call"
                            _ptrace(libc, PTRACE_KILL, pid)
                            break
                        regs.rip = return_pc
                        _setregs(libc, pid, regs)
                        continue
                    if os.WIFSTOPPED(status):
                        result["complete"] = False
                        result["stop_reason"] = f"signal_{os.WSTOPSIG(status)}_while_skipping_host_call"
                        _ptrace(libc, PTRACE_KILL, pid)
                    break
                _ptrace(libc, PTRACE_CONT, pid)
                _, status = os.waitpid(pid, 0)
                if os.WIFSTOPPED(status):
                    result["complete"] = False
                    result["stop_reason"] = f"signal_{os.WSTOPSIG(status)}_outside_instrumented_ranges"
                    _ptrace(libc, PTRACE_KILL, pid)
                break
            except OSError as error:
                result["complete"] = False
                result["stop_reason"] = f"ptrace_error:{error.errno}"
                break
        if result["instructions"] >= instruction_limit:
            result["complete"] = False
            result["stop_reason"] = "instruction_limit"
            _ptrace(libc, PTRACE_CONT, pid)
            _, status = os.waitpid(pid, 0)
            break
        code = _read_memory(libc, pid, pc, 15)
        try:
            insn = decoder.decode(pc, code or b"")
        except TraceUnavailable as error:
            active_mnemonic = "<undecoded>"
            mark_gap(pc, str(error), capability="instruction_decode")
            result["stop_reason"] = "unsupported_instruction_decode"
            _ptrace(libc, PTRACE_KILL, pid)
            _, status = os.waitpid(pid, 0)
            break
        raw = bytes.fromhex(insn["bytes"])
        active_mnemonic = insn["mnemonic"]
        issue = _instruction_support_issue(insn)
        if issue:
            capability, reason = issue
            mark_gap(pc, reason, insn["mnemonic"], capability)
            result["stop_reason"] = "unsupported_instruction"
            # Do not execute an instruction whose memory or privileged side effects are
            # outside the trace model. A partial trace can never look complete or mutate
            # the fixture further after the unsupported operation.
            _ptrace(libc, PTRACE_KILL, pid)
            _, status = os.waitpid(pid, 0)
            break
        events = []
        mnemonic = insn["mnemonic"].lower().split()[-1]
        pop_has_memory_destination = False
        unsupported_operand = None
        for index, operand in enumerate(insn["operands"]):
            access = operand["access"]
            if access not in (CS_AC_READ, CS_AC_WRITE, CS_AC_READ | CS_AC_WRITE):
                unsupported_operand = ("memory_access_classification",
                                       f"decoder did not classify memory operand {index}")
                break
            pop_rsp_increment = 0
            if mnemonic == "pop":
                pop_has_memory_destination = True
                if access != CS_AC_WRITE:
                    unsupported_operand = ("stack_memory", "POP memory destination access classification is unsupported")
                    break
                base, index_register = operand["base"], operand["index"]
                if operand["size"] not in (2, 8) or insn["address_size"] not in (4, 8):
                    unsupported_operand = ("stack_memory", "POP memory destination has unsupported operand or address size")
                    break
                if index_register in ("rsp", "esp"):
                    unsupported_operand = ("stack_memory", "POP memory destination uses unsupported RSP index encoding")
                    break
                if base in ("rsp", "esp"):
                    pop_rsp_increment = operand["size"]
            address, address_gaps = _effective_address(operand, regs, pc, insn["size"],
                                                       insn["address_size"], pop_rsp_increment)
            for gap in address_gaps:
                unsupported_operand = ("effective_address", gap)
            if address is None:
                break
            events.append(_event(address, operand["size"], bool(access & CS_AC_READ),
                                 bool(access & CS_AC_WRITE), regs, pc, identity, regions,
                                 libc, pid, insn, index))
        if unsupported_operand:
            capability, reason = unsupported_operand
            mark_gap(pc, reason, insn["mnemonic"], capability)
            result["stop_reason"] = "unsupported_instruction"
            _ptrace(libc, PTRACE_KILL, pid)
            _, status = os.waitpid(pid, 0)
            break
        # x86 stack effects not always exposed as explicit memory operands.
        implicit = None
        stack_width = 8
        if mnemonic in ("push", "pop"):
            stack_width = insn["operand_sizes"][0] if insn["operand_sizes"] else 0
            if stack_width not in (2, 8):
                mark_gap(pc, f"stack operand width {stack_width} is unsupported",
                         insn["mnemonic"], "stack_width")
                result["stop_reason"] = "unsupported_instruction"
                _ptrace(libc, PTRACE_KILL, pid)
                _, status = os.waitpid(pid, 0)
                break
        if mnemonic in ("pushf", "pushfd", "pushfq", "popf", "popfd", "popfq"):
            stack_width = 2 if 0x66 in insn["prefixes"] else 8
        if mnemonic == "call" or mnemonic == "push" or mnemonic in ("pushf", "pushfd", "pushfq"):
            implicit = (regs.rsp - stack_width, stack_width, False, True, None)
        elif mnemonic == "ret" or mnemonic == "pop" or mnemonic in ("popf", "popfd", "popfq"):
            implicit = (regs.rsp, stack_width, True, False, None)
        elif mnemonic == "leave":
            implicit = (regs.rbp, 8, True, False, None)
        if implicit:
            address, width, read, write, _ = implicit
            implicit_event = _event(address, width, read, write, regs, pc, identity, regions,
                                    libc, pid, insn, None)
            events.append(implicit_event)
            if mnemonic == "pop" and pop_has_memory_destination:
                # The source stack read precedes the destination write architecturally.
                events.insert(0, events.pop())
        _ptrace(libc, PTRACE_SINGLESTEP, pid)
        _, status = os.waitpid(pid, 0)
        result["instructions"] += 1
        if not os.WIFSTOPPED(status):
            break
        after_regs = _regs(libc, pid)
        next_pc = after_regs.rip
        flow_kind = insn["control_flow"]
        branch_target = insn["branch_target"]
        flow_outcome = None
        fallthrough = pc + insn["size"]
        if flow_kind == "conditional":
            if branch_target is None:
                cfg_error = f"conditional branch at 0x{pc:x} has no decoded target"
            elif next_pc == branch_target:
                flow_outcome = "taken"
            elif next_pc == fallthrough:
                flow_outcome = "fallthrough"
            else:
                cfg_error = f"conditional branch at 0x{pc:x} reached an unexpected target"
        elif flow_kind in ("jump", "call"):
            if branch_target is not None and next_pc != branch_target:
                cfg_error = f"direct {flow_kind} at 0x{pc:x} did not reach its decoded target"
            flow_outcome = "direct" if branch_target is not None else "indirect"
        elif flow_kind == "return":
            flow_outcome = "return"
        elif next_pc != fallthrough:
            cfg_error = f"unclassified control transfer at 0x{pc:x} ({insn['mnemonic']})"
        next_identity = next((label for lo, hi, label in ranges if lo <= next_pc < hi), None)
        if flow_kind and flow_kind != "return" and next_identity is None and cfg_error is None:
            cfg_error = f"{flow_kind} at 0x{pc:x} leaves instrumented code ranges"
        if len(cfg_steps) < MAX_CFG_STEPS:
            cfg_steps.append((identity, pc, next_pc, flow_kind, branch_target, flow_outcome, insn["size"]))
        else:
            cfg_collection_truncated = True
        if cfg_error:
            mark_gap(pc, cfg_error, insn["mnemonic"], "control_flow")
            if "unclassified control transfer" in cfg_error or "unexpected target" in cfg_error:
                result["stop_reason"] = "unclassified_control_flow"
        for item in events:
            if item["access"] in ("read", "read_write") and item["before"] is None and item["valid_region"]:
                mark_gap(pc, "read bytes could not be observed")
            if item["access"] in ("write", "read_write"):
                after = _read_memory(libc, pid, int(item["address"], 16), item["width"])
                item["after"] = after.hex() if after is not None else None
                if after is None and item["valid_region"]:
                    mark_gap(pc, "write result bytes could not be observed")
            item["instruction_after"] = f"0x{after_regs.rip:x}"
            item["state_after"] = {name: f"0x{getattr(after_regs, name):x}" for name in
                                   ("rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", "r8", "r9", "r10", "r11", "rip")}
            item["stack_delta"] = after_regs.rsp - entry_sp
            item["valid_region"] = item["valid_region"] and _region(int(item["address"], 16), item["width"],
                                                                       item["access"] in ("write", "read_write"), regions)[1]
            if not item["valid_region"]:
                result["complete"] = False
                item["bounds_violation"] = True
            result["events"].append(item)
            if len(result["events"]) >= MAX_EVENTS:
                result["complete"] = False
                result["stop_reason"] = "event_limit"
                _ptrace(libc, PTRACE_CONT, pid)
                _, status = os.waitpid(pid, 0)
                break
        if result.get("stop_reason") == "event_limit":
            break
        if result.get("stop_reason") == "unclassified_control_flow":
            _ptrace(libc, PTRACE_KILL, pid)
            _, status = os.waitpid(pid, 0)
            break
        if os.WIFSIGNALED(status) or os.WIFEXITED(status):
            break
        if os.WSTOPSIG(status) != signal.SIGTRAP:
            signum = os.WSTOPSIG(status)
            mark_gap(pc, f"instruction stopped with signal {signum}",
                     active_mnemonic, "instruction_exception")
            result["stop_reason"] = f"instruction_signal_{signum}"
            _ptrace(libc, PTRACE_KILL, pid)
            _, status = os.waitpid(pid, 0)
            break
    result["gaps"].extend({"instruction": f"0x{pc:x}", "mnemonic": mnemonic,
                           "capability": capability, "reason": reason}
                         for pc, mnemonic, capability, reason in sorted(gaps_seen))
    result["trace_version"] = "capstone5-ptrace-x86_64-v2"
    result["identity"] = "original_or_replacement_guest_instructions"
    result["control_flow_coverage"] = _control_flow_coverage(
        cfg_steps, ranges, result["complete"], result.get("stop_reason"),
        cfg_collection_truncated, cfg_error)
    if os.WIFSTOPPED(status):
        _ptrace(libc, PTRACE_KILL, pid)
        _, status = os.waitpid(pid, 0)
    result["_tracee_wait_status"] = status
    return result


def trace_me() -> None:
    """Stop a just-forked harness child so its parent can attach as the ptrace tracer."""
    libc = ctypes.CDLL(None, use_errno=True)
    libc.ptrace.restype = ctypes.c_long
    libc.ptrace.argtypes = [ctypes.c_uint, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p]
    _ptrace(libc, PTRACE_TRACEME, 0)
    os.kill(os.getpid(), signal.SIGSTOP)
