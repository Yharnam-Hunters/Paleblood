# GFX7 volume tiling

`runtime/volume_tiling.h` describes a single-mip, uncompressed 1D-thick volume.
Its profile combines `1D_TILED_THICK` geometry with `ADDR_THICK` micro-order.
The 8×8×4 microtiles interleave spatial Z with X/Y; Z is not an array layer.
The bit order depends on the element size:

| Bytes per element | Microtile bits, low to high |
| --- | --- |
| 1, 2 | x0, y0, x1, y1, z0, z1, x2, y2 |
| 4 | x0, y0, x1, z0, y1, z1, x2, y2 |
| 8, 16 | x0, y0, z0, x1, y1, z1, x2, y2 |

These are AMD AddrLib's [thick microtile rules](https://github.com/GPUOpen-Drivers/pal/blob/7ce51b199c72021b8d027c50b55da515fd9b2e0b/src/core/imported/addrlib/src/core/addrlib1.cpp#L3087-L3184).
Each microtile contains 256 texels. Complete tiles advance across pitch, then
height, then groups of four Z coordinates, following the [microtiled address formula](https://github.com/GPUOpen-Drivers/pal/blob/7ce51b199c72021b8d027c50b55da515fd9b2e0b/src/core/imported/addrlib/src/r800/egbaddrlib.cpp#L1906-L1986).
The physical pitch must cover visible width and be a multiple of eight. Height
rounds to eight and depth to four. Size arithmetic is checked before publishing
the layout; failure preserves the caller's output.

The admitted tile index is 19, following [AMD's CI table convention](https://github.com/GPUOpen-Drivers/pal/blob/7ce51b199c72021b8d027c50b55da515fd9b2e0b/src/core/imported/addrlib/src/r800/ciaddrlib.cpp#L1423-L1431).
That convention does not establish an arbitrary guest's programmed table. The
profile excludes display, depth/stereo surfaces, alternate tile settings, macro
3D tiling and mip chains. `POW2_PAD` is accepted only when dimensions and pitch
are already powers of two, requiring no additional power-of-two growth; ordinary
microtile padding still applies. Broader [power-of-two volume padding](https://github.com/GPUOpen-Drivers/pal/blob/7ce51b199c72021b8d027c50b55da515fd9b2e0b/src/core/imported/addrlib/src/core/addrlib1.cpp#L3829-L3843)
remains outside this profile.

Tile/detile copies operate on visible texels and preserve every padded texel.
Padding is interleaved inside microtiles, so preserving a trailing byte range is
insufficient. This common CPU mapper does not itself enable native 3D textures,
GPU upload/readback, guest coherence or a complete game boot.
