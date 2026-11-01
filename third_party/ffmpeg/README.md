# Private HEVC decoder backport

The private LGPL-2.1-or-later decoder uses the authenticated FFmpeg8.1.3 release
pinned by `packaging/mask-ffmpeg.json`. It is separate from Steam/Valve libraries.
The unchanged release archive, detached signature and public signing key are in
`inputs/` so verified builds do not depend on a live connection to ffmpeg.org.
The builder still checks every pinned hash and the release-signing fingerprint.
The source archive retains upstream's complete license and attribution notices.

`packaging/mask-ffmpeg-patches.json` pins the ordered patches and every predecessor
and result source hash, the original proposal archive and attribution. The slice
implementation comes from Phillip Blucas's independent HEVC NAL slice threading
proposal, [FFmpeg pull request23873](https://code.ffmpeg.org/FFmpeg/FFmpeg/pulls/23873).
The patch header preserves the original authorship. The adaptation targets the
8.1.3 weighted-prediction and WPP APIs; no FATE sample redistribution is needed.

Our additional changes keep precomputed slice ownership immutable while jobs
run and require complete CTB coverage before accepting a software picture under
`AV_EF_EXPLODE`. Invalid/incomplete output cannot become a current mask. Deblocking
and SAO run after slice jobs join. Unsupported picture layouts retain the existing
serial/WPP handling; no filter or output byte is intentionally changed.

The separate local `0002-hevc-luma-only-8.1.3.patch` implements an explicit
`AV_CODEC_FLAG_GRAY` optimization for Main8/4:2:0 only. The mask consumer uses Y;
unused chroma prediction and chroma deblocking/SAO are omitted. All syntax,
entropy/residual parsing, neighboring metadata, luma prediction, luma filtering,
slice synchronization and complete-picture checks remain. Chroma samples are
unspecified in this explicit mode and must not be sampled. Normal color decoding
and other profiles retain complete reconstruction. This local change is not part
of the upstream slice-threading proposal. See
[the candidate and its measured scope](../../docs/development/MASK-LUMA-ONLY.md).

`tools/build_mask_hevc_ffmpeg.py --help` documents offline input/build arguments.
It requires GNU patch as well as the ordinary compiler, make and GnuPG toolchain.
The tool authenticates the release, applies the byte-pinned patch chain in scratch space,
verifies every result before replacing cached source, then verifies all upstream
regular files. Edited or partially patched caches are refused. Each build recipe
records the actual patch set and resulting private library hashes. `--upstream-only`
is restricted to a separate host comparison output, never a Frame production build.

Owned complete non-WPP two/four-slice fixtures, SAO and cross-slice filter cases,
partial-picture refusal and source/packet/DPB tests live in `tests/mask-hevc/`.
Base fixture YUV equals encoder reconstruction; filter variants compare every Y
byte against an unmodified serial decoder. They contain no game/device captures.
The original LGPL text, release lock, patch manifest, backport and build tool are
included with the distributed private libraries, including both patches. No
GPL/nonfree options are used.
