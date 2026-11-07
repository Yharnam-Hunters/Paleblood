# VideoOut matching-key cleanup

`rt_video_out_clear_matching_output_key` expresses the private matching-key cleanup
helper recovered from the supplied VideoOut module. Its unwind-bounded body is
126 bytes at module offset `0xdcd0`; the lower output helper is at `0xd800`.
These are module offsets, not entries in the game's executable function catalog.
The original module SHA-256 is
`33a6f16349f07637e168db0d6338f0b2ae8aea626b86bc3d28325ecb5155e2c1`.
Raw original bytes, listings and native comparison results stay private.

The helper compares the input's low DWORD with the module's key DWORD
(offset `0x1d6e8`). A mismatch returns zero without reading the companion DWORD
(`0x1d6ec`) or calling the output helper. A match captures the companion value,
constructs a 32-byte configuration with a size of 32 and all remaining bytes FF,
and calls the lower helper with `(0, configuration, null, 0, 0, key, companion)`.
On every normal return it clears only the key DWORD and returns zero, regardless
of the lower helper's result or a change to the key during the call. Cleanup
itself does not write the companion DWORD.

The native caller writes the seventh argument's low DWORD; the lower helper
reads that DWORD. Its upper stack-slot bytes are not a logical argument. No
pointer to that caller slot escapes in the observed calls, so the source uses an
ordinary by-value `uint32_t` parameter. The configuration is the only local object
passed to the lower helper. Compiler canaries, unrelated spill slots and ambient
register contents are omitted. Access beyond the declared configuration object
and arbitrary asynchronous stack inspection are outside this logical contract.

The module owner must supply both actual word addresses and the real callable
lower helper through `rt_video_out_output_key_bindings`. Missing reached bindings
stop by name. This contribution does not install a native export, implement the
lower helper, or provide an AV service response. It has no dependency on the older
source-only output-helper frame model.

The dedicated runtime unit test covers matching and nonmatching full-DWORD keys,
sentinel bytes and call arguments, companion capture, negative and positive
results, callback changes and adjacent-word preservation. Same-cell binding
aliases and missing-binding stops are owned-interface tests, not claims about
the native module's distinct adjacent cells. Native differential evidence
compares this helper with its original at the scripted lower-helper boundary;
it does not establish the lower helper's implementation or a genuine game run.
