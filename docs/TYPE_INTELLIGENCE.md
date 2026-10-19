# Type intelligence and conservative classification

Type metadata can connect otherwise anonymous functions. It does not establish a function's ABI,
behavior, fixture requirements, or verification status. All executable analysis stays local.

## Export facts and candidates

`tools/ghidra/ExportTypeIntelligence.java` reads the existing analyzed program without modifying it:

```sh
analyzeHeadless "$PROJECT_DIR" "$PROJECT_NAME" -process eboot.elf -readOnly -noanalysis \
  -scriptPath tools/ghidra -postScript ExportTypeIntelligence.java "$PRIVATE_OUTPUT/types.json"
```

The program must be little-endian and use image base `0x400000`. The output records its executable
SHA-256, Ghidra/exporter versions, scan budget, truncation and limitations. It exports addresses,
names, pointer relationships and confidence, never instruction bytes or decompiler listings.
Keep this metadata outside the public repository too.

The bounded scan examines initialized readable non-executable memory at eight-byte alignment.
It looks for an Itanium-shaped offset-to-top/typeinfo header followed by pointers to existing
Ghidra function entries. A nonzero typeinfo reference needs a mapped vptr/name-pointer pair and
a bounded, plausible encoded class name. Metadata names may be in readable executable mappings;
execute permission alone does not invalidate a data pointer. References to imported RTTI
meta-vtable symbols also identify standalone typeinfo records without a local vtable body.
An identified meta-vtable symbol at `vptr - 16` can reside in Ghidra's uninitialized
external-symbol block: its body is not read or presumed present. Unidentified unmapped
or uninitialized vptrs remain rejected. Address-derived `class_<hex>` namespace labels
remain unnamed rather than becoming recovered class names.
A zero typeinfo reference needs at least two function
pointers and remains an **untyped pointer-table candidate**, which may be another kind of table.
Positive offset-to-top construction tables, relative/encoded tables, arbitrary name strings,
unrecognized custom RTTI, and pointers without Ghidra functions are not recovered by this scan.

Tables retain at most 256 slots and 64 references each. Their full extent is unknown. Repeated
functions in slots, secondary tables with negative offsets, and distinct tables sharing functions
remain separate relationships. Reference discovery is a candidate association, not evidence that
the referencing function is a constructor. Constructor/destructor candidates are separately
labeled only when existing class-namespace symbols support them.

The generic [Itanium C++ ABI](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#vtable) describes the
layout used as a structural hypothesis. This is not a declaration that all target classes conform
to it. Base relationships are parsed only when an existing RTTI meta-vtable symbol identifies the
single- or multiple-inheritance layout; otherwise inheritance remains unknown, including when
the base list is empty. Base offset/flag words are retained as candidate metadata, not object-layout
guarantees. A stripped or unsupported RTTI representation must not acquire invented names.

## Import into the existing catalog

```sh
python3 tools/function_catalog.py --state "$CATALOG" intelligence import "$PRIVATE_OUTPUT/types.json"
python3 tools/function_catalog.py --state "$CATALOG" intelligence status
python3 tools/function_catalog.py --state "$CATALOG" intelligence types --limit 20
python3 tools/function_catalog.py --state "$CATALOG" intelligence vtables --limit 20
python3 tools/function_catalog.py --state "$CATALOG" function 0x01234567
python3 tools/function_catalog.py --state "$CATALOG" rank --limit 20
```

The importer requires the catalog's exact executable identity. It merges identical records,
retains conflicting names/bases, and reports ambiguity. Optional `c++filt -t` decodes recovered
encoded names; unavailable decoding leaves the encoded name intact. Conflicting names are not
selected by arrival order. Missing referenced types remain unresolved references.

An atomic SQLite transaction updates the existing function rows, source identity and one
`intelligence_snapshot` table in the same catalog. Interrupted imports roll back. Identical
reimports are idempotent. Changed artifact content, mappings or importer version changes catalog
identity and invalidates qualification decisions. Rebuilding without the intelligence artifact
clears the overlay; the original catalog schema and canonical function status files are unchanged.

The summary separates named type records, memory RTTI records, symbol namespaces, ambiguous types,
typed/untyped table candidates and associated catalog functions. **None is a count of independently
confirmed classes or verified behaviors.** Imported library namespaces need not describe classes
implemented by the executable.

## Classification and community evidence

Categories are `game_engine`, `middleware`, `runtime_library`, `compiler_generated`, and `unknown`.
Totals and percentages use the entire catalog function count as the denominator. Unsupported
classification remains unknown. Current automatic positive rules are deliberately narrow:
Ghidra thunk analysis supports a compiler-thunk label; a recovered `std::` function symbol
supports a library label. These are analysis/symbol classifications, not behavioral verification.
Virtual slots in unambiguous RTTI-named `FMOD::` tables support a structural middleware-origin
hypothesis, citing the vendor's [published namespace usage](https://www.fmod.com/docs/2.04/api/studio-api-getting-started.html).
Table references from callers do not support that classification. This rule identifies neither
the target's middleware version nor the behavior of a particular function.
The committed denominator excludes thunks, so importing thunk symbols may classify zero catalog
functions. Conflicting positive classifications remain unknown with their evidence preserved.

Optional `--community-mappings research.json` accepts exact type-name matches:

```json
{"schema": 1, "mappings": [{
  "type_name": "SyntheticActor", "label": "possible actor family", "category": "game_engine",
  "source_url": "https://example.org/research", "license": "Apache-2.0",
  "revision": "exact-source-revision", "attribution": "Research author", "source_game": "another title"
}]}
```

This is a synthetic format example. Before importing a real mapping, review the actual source,
license, attribution requirements and exact revision. A declared license is a provenance record,
not automatic permission. Do not import decompiled implementations, game layouts from unlicensed
dumps, or proprietary assets. [fromsoftware-rs](https://github.com/vswarte/fromsoftware-rs) is a
potential naming reference, but its current repository contains custom non-AI license files despite
the package metadata's standard license expression. Reassess the exact revision and applicable
license before use; no labels from that revision were imported here. Unknown, unlicensed,
`NOASSERTION`, and explicitly non-AI mapping licenses are rejected by this tool. A license field
alone cannot detect undisclosed source restrictions. An Elden Ring, Sekiro or Dark Souls 3 similarity always remains candidate
evidence here; it cannot prove Bloodborne layouts or behavior. No community mapping changes
canonical names, ABI qualifications, function status or verification evidence.

For an optional Ghidra integration regression, compile the project-owned metadata fixture:
`cc -fno-pie -no-pie test/fixtures/type_intelligence.c -o "$PRIVATE_OUTPUT/type-fixture.elf"`.
Import/analyze that ELF in a separate scratch project and run the same exporter. It provides
single/multiple-inheritance-shaped records, a secondary table and stripped table, without target
bytes, including an uninitialized symbol-backed meta-vtable and an `St` standard-namespace encoded
name. This tests extraction mechanics only; the normal Python suite tests normalization,
conflicts, duplicate records, cache identity, atomic rollback and ranking.

The explainable `type_context` ranking component defaults to weight 10. Unambiguous named
associations provide context; unnamed tables provide a small heuristic signal; unknown context
provides none. Call graph, complexity, execution feasibility and ownership scores remain separate.
Ranking never makes a blocked function eligible. See [FUNCTION_CATALOG.md](FUNCTION_CATALOG.md)
and [AUTOMATION_PIPELINE.md](AUTOMATION_PIPELINE.md) for the existing gates.
