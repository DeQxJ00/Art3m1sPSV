# Recovered E-mote runtime

This directory contains only the runtime translation units and their local
headers used by the integrated Vita host. The standalone demo renderer,
binary analysis inputs and test assets are not build dependencies.

`source-manifest.json` pins the recovery repository commit and the exact source
bytes used by the device-tested backend. Run
`python scripts/recovered-source-manifest.py vendor/emote-recovery` from the
project root before building. When updating the runtime, copy the required
sources from the audited recovery checkout and regenerate the manifest together.
The file set and hashes must agree on both Windows and Linux.

CI builds the Rust core with `emote-recovery`, then uses the same host build
script and release flags as the device test. `build-info.json` identifies the
default backend, core revision and recovery source manifest. Different build
platforms and release metadata can change executable hashes; a matching source
manifest is not a claim of byte-identical executables.
