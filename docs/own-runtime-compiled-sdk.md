# Compiled SDK version

`sceKernelGetCompiledSdkVersion` reads the main executable's process-parameter
metadata on every call and publishes its SDK-version DWORD. The own-runtime
provider uses the existing loader-owned `rt_process.proc_param` within
`rt_process.image` and `image_size`; it does not call a console syscall, read SFO,
choose a default SDK or cache a guessed version.

Independent static analysis of the supplied libkernel binds the getter export
NID `WB66evu8bsU` to ELF `0x1a920`, 126 bytes. Its local contract is:

- NULL output returns numeric status `0x80020016` before the provider call.
- A nonzero provider result, NULL returned pointer, unsigned declared size below
  20, magic other than `ORBI`, or zero version returns the same status.
- Otherwise it reads the DWORD at offset 16, writes exactly four bytes to the
  caller's output, and returns zero. It accepts every SDK bit pattern and every
  nonzero version. It captures the value before writing an aliased output.

REA evidence `ev_cb3c43b891a92015044b910ad0af67a5d8067884a88826e8ed03273ce8c67c20`
binds the getter's complete body. Its 24-byte direct helper at ELF `0x1d70`
issues syscall `0x256` and forwards its actual result/carry handling (evidence
`ev_63f00a2669409c3363877b09c6ff88f3a41ee3b977f338268d3a3d6622b73d22`).
The helper contains no output stores. Its kernel handler, returned storage and
second output slot's meaning were not recovered or executed. The getter does
not read that second slot. Decompiler text that retains the syscall number as
its result is not a valid substitute for the assembly.

The source library SHA-256 is
`b3e13917da667d01a353ccf49025d6b1308a32cf86f3f351f96b3a13cd5c7c2f`.
No library bytes or raw decompiler output are included here, and no other
runtime or emulator implementation was used.

The supported host domain is ordinary stable metadata with at least 20 readable
bytes inside the owned mapped main image, and a writable four-byte output on
success. Unaligned storage and output aliases use `memcpy`. A NULL process
parameter returns the locally established status. An inconsistent host mapping
stops by API name before any metadata read; no guest error is guessed for that
host configuration. Concurrent metadata mutation, MMIO and arbitrary invalid
non-NULL output pointers are outside this domain. This is a declared metadata
provider policy, not proof of console kernel behavior or all firmware versions.

The dedicated `compiled_sdk` registry test keeps checks active in Release and
covers exact output/canaries, unaligned source/output, full-width values,
nonzero versions, unsigned sizes, rejected headers, aliases, refresh between
calls and unsupported host mapping guards. It supplies synthetic bytes only.
It does not establish AppContent service outputs or successful module startup.
