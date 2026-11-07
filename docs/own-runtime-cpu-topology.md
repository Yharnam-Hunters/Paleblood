# Own-runtime CPU topology

Paleblood exposes its own CPU topology. It does not claim the CPU mask of a console or infer
one from a core count. The current implementation covers the registered calling main thread;
upstream guest thread creation and recorded affinity setters are preserved. Main-thread queries use the actual mapped host affinity; worker attributes retain their explicitly recorded guest mask. Host affinity is never changed by this component.

The mapping core and provider interface are independent of Linux. The current native provider
is Linux-only; other hosts, including macOS, fail clearly until a platform provider is supplied.
A Darwin provider must define its own honest CPU/scheduling policy instead of pretending that
Linux hard-affinity APIs exist there. This unit supplies no macOS execution or graphics backend.

Before guest code runs, the Linux backend queries that host thread's actual allowed CPUs using
a dynamically sized CPU set. By default, all allowed host CPU IDs in ascending order become
guest bits 0 through N-1. There must be between 1 and 64 IDs. A host ID may be sparse or greater
than 63: it is mapped to a guest bit, never copied or truncated. The boot prints the complete
mapping and labels it as port policy.

`BB_RUNTIME_CPU_MAP` optionally specifies an ordered comma-separated list of decimal host IDs.
It must be a permutation of the same entire allowed set. For example, a host allowed set
`5,130,4097` normally maps guest bits `0,1,2` to those IDs. An explicit `4097,130,5` reverses
that order. No setting silently discards CPUs or changes host affinity. If more than 64 CPUs
are allowed, narrow the host process's affinity externally before starting Paleblood. Invalid
settings and unsupported host queries stop before guest execution.

`scePthreadAttrGet` re-queries the actual calling registered main thread and snapshots its
translated mask. A later subset retains its original guest bit positions. A newly allowed
host ID outside the original map stops with a named diagnostic; it is not ignored. Attribute
getters then read the saved snapshot, not the live host state. The provider/context API used
by embedding hosts and unit tests has the same ownership and mapping rules.

The supplied plaintext libkernel image has SHA256
`b3e13917da667d01a353ccf49025d6b1308a32cf86f3f351f96b3a13cd5c7c2f`.
Static analysis of that exact image establishes the two-pointer SCE affinity getter, one
eight-byte store on success, low32 error translation, null/empty attribute EINVAL, and the
difference between a fresh unrestricted attribute and one filled by a thread-affinity query.
Its build/firmware compatibility with the game's bundled library has not been established.
No target library or raw decompiler output is included in the repository.

Paleblood chooses an internal 16-byte CPU-set policy with at most 64 mapped guest CPUs. A fresh
attribute remains unrestricted, so its public low64 affinity is all ones. A copied main-thread
attribute contains the actual mapped snapshot. This internal width and topology are declared
port choices; the target kernel's CPU-set size and default numeric mask remain unmeasured.

The public getter publishes exactly eight bytes with `memcpy`, permitting unaligned or aliased
valid output storage. A null/empty attribute returns `0x80020016` and leaves output unchanged.
The observed target does not check output nullness on success, so no new null-output EINVAL is
invented. Backend query failures or an unsupported thread produce named diagnostics instead
of invented BSD errors or a fabricated mask. Other attribute behavior, including the unresolved
detach-state policy, remains as documented previously.

Future create/set/current-CPU/scheduler interfaces must use this same guest-bit mapping and
apply the corresponding host CPU set. A fixed game mask must retain its logical meaning.
Zero-mask validation and the supplied nested setter's errors require their own contract review;
this unit does not register a setter or pretend to support guest worker threads.

`test_affinity` uses only mocked providers. It covers sparse/high host IDs, explicit mapping,
bit63, rejected truncation, fresh versus captured masks, live subset changes, unknown CPU
expansion, eight-byte canaries, alias output, invalid-attribute errors and provider failure.
Actual Linux backend/build/boot validation is separate from these planned unit tests.
