# Projected game alpha

Development candidate: installer0.3.37, PC0.3.24, Frame0.3.13.
Software validation is separate from headset acceptance.

The current PC investigation preserves coverage before the native multiview
scene/resolve programs make their streaming tiles opaque. The ordinary native
RGB pipeline keeps its resources, programs, constants and rendering operations.
A companion replay uses separate storage and the same current geometry,
sampling, filtering and reprojection bindings. Private FP16 intermediate images
retain the RGBA needed by later native filters; only the final alpha image is
shared with the sender. Both reviewed 64-byte and 80-byte scene policies and
the four-sample resolve policy are retained.

Production uses an unnamed kernel section duplicated by the verified loader
into the exact compositor process instance. The named SourceAudit V1 records
remain diagnostics. The sender publishes only its current retained native
opens, their actual kernel shapes and the live application lease. Numerical
shared-handle reuse cannot bypass the retained-allocation publication gate.

A root sample requires the actual application allocation and its observed
successful native keyed-mutex ownership. Complete image coverage requires the
actual vertex buffer, input layout, vertex range, viewport, clipping and native
program contract. A partial X replay can start from an observed zero-RGBA native
clear. Other native writes revoke the affected image before forwarding. Unknown
command lists revoke the graph. No native shader, key colour, approximate time
or compositor presentation count supplies missing image ancestry.

The producer seals each RGB/alpha image version at its original native mutex
release. Multiple nested ownership scopes remain separate choices. The sender
selects only the choice matching its actual Present sync texture and successful
consumer acquisition key, then copies coverage through the existing exact
copy-view and accepted-NVENC-input chain. The actual compositor producer PID is
retained separately from the authenticated game's mask source PID. An active
game without this proof uses camera-only recovery.

Resources are bounded to 32 published images and 256 MiB of companion texture
storage across the private producer channel. Preparation runs on native resource
creation. Rendering callbacks perform no allocation, shader compilation or
blocking synchronization. Calls follow the actual native caller. A nonblocking
per-context guard permits sequential thread migration; unsupported concurrent
mutation retires the private channel. Previously supported direct
application RGBA routes remain available.

Windows fixtures exercise embedded owned kernels, original RGB preservation,
zero/fractional/opaque coverage, geometry, FP16 filtering, MSAA, native ownership
transfers and private loader/channel boundaries. These checks do not establish
a headset result.

## Decoder allocation padding

The Frame queries the public raw CAPTURE format and COMPOSE rectangle after a
successful native decode. The exact read-only capture lease retains these
integers along with its DMA allocation/content identity. Format, pool, queue or
crop/compose changes revoke the old proof. The local channel uses SFL3 and the
capture lease backing uses V2; both endpoints ship together in the Frame patch.

An immutable per-mask uniform maps native allocation UVs into coded mask UVs.
The original RGB sampling, filtering and foveation weights remain native.
Sampling outside a verified visible rectangle produces zero RGBA when it could
otherwise consume padding. Identity mappings retain the previous shader output,
and ordinary pictures retain native output. A padded image with missing or
inconsistent geometry cannot acquire game rendering permission.

The geometry uniform is frozen before the first recording reference. The GPU
pool retains it through the same exact frame, recording and submission lifetime
as the R8 mask. No latest geometry, guessed zero origin or fixed resolution is
used. Real Vulkan controls cover old/current native programs, fractional alpha,
nonzero origins, padding, ordinary mode and zero-RGBA recovery.
