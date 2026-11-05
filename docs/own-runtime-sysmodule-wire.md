# System-module IDs and early errors

The libSceSysmodule implementation uses the complete EDI DWORD for module IDs. The
previous runtime implementation discarded its upper 16 bits, allowing a
request such as `0xabcd7f01` to act on the declared stub `0x7f01`. The runtime now
keeps the full argument through validation and diagnostics. Its explicitly
configured stub namespace remains 16 bits; other full-width requests stop by
API name before lookup or mutation.

Independent disassembly establishes these early returns:

- `sceSysmoduleLoadModule`: `(id32 & 0x7fffffff) == 0` returns `0x805a1000` before
  any mode predicate, lock or load delegate. This includes IDs 0 and 0x80000000.
- `sceSysmoduleIsLoaded` and `sceSysmoduleUnloadModule`: ID 0 returns the same
  numeric status before their lock or state delegate. Their 0x80000000 behavior
  is delegated; the runtime does not invent an answer.

These returns leave stub records unchanged, even if a host configuration
contains a zero-ID record. Other wider requests are an explicit unsupported
host-policy boundary, not a claim that the original always returns this status
or always fails. In particular the native load validator distinguishes exact
IDs 0x80/0xb0/0xb8 from other full-width values before passing the original DWORD
to category predicates. Truncation would select a different validation path even where eventual return
values remain unknown.

Evidence is from the library with SHA-256
`43c456e00b59b2bd1303023cbcb463f50c0647854b4837468f8fc6c290b55c23`:
load at ELF 0x410 (101 bytes), query at 0x580 (74), unload at 0x520 (81), and
load validator at 0x480 (149). The four complete bodies contain 135 instructions.
The function boundaries were checked against bytes and unwind records. No firmware
execution or other emulator/runtime implementation was used. These observations identify this library version; they do not establish behavior for every firmware.

Native wrappers also handle mutex failures and delegated statuses; loading
remaps only numeric 0x80020064 to 0x80020016 after a successful lock. Their inner
load/state/unload methods, mode globals, category checks, duplicate/refcount and
release behavior remain unresolved. Those facts do not justify replacing the
existing explicit stub policy with real module-loading success. AppContent
0xb4 is still unsupported; no new module is silently marked loaded.

The registry test checks known numeric errors,
complete-ID diagnostics, preservation of all stub records on errors/stops,
wide requests against both loaded and unloaded stubs, and the retained narrow
stub lifecycle. The older aggregate registry test now expects a wide unload to
stop instead of accepting an alias. Synthetic stub IDs describe host policy;
they do not claim admission by the native validator.
