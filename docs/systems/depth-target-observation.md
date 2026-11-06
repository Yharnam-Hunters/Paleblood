# Depth-target observation

The depth-target replacement reports final register packets through the canonical observer.
A registered replacement suppresses the original-only hook, so the replacement must retain
observation itself. It snapshots begin/offset after successful reservation, then reads output
only after every store and final cursor publication. Borrowed descriptors are not reread.

The first packet carries eight values from `DB_Z_INFO` through `DB_DEPTH_SLICE`. Four singleton
packets carry `DB_DEPTH_INFO`, `DB_DEPTH_VIEW`, `DB_HTILE_DATA_BASE` and `DB_HTILE_SURFACE`.
Their final headers, selections and nonoverlapping forward offsets must be supported before
any register is recorded. Each record uses its actual packet offset relative to the saved
begin. Unsupported partial layouts fail explicitly. A disabled target records only zero
`DB_Z_INFO` and `DB_STENCIL_INFO`, after validating both final clear packets.

Hosts without a canonical recorder gain no observer-only begin or output reads. Separate
owned tests use the real recorder and cover callback changes, declined callbacks, descriptor
output overlap, and final cursor publication changing depth words while packet headers remain
valid. Native original/replacement fixtures remain separate. Depth/stencil format and operation
support follows the current renderer; this observer introduces no new runtime API.
