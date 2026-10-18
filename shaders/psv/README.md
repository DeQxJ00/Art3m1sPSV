# PSV effect implementations

This directory contains one flat PC-derived set of 33 implementations. Source
platforms do not create extra copies of an effect.

- `catalog.json`: exact source identities and canonical AGX1 packages.
- `parameters.json`: generated float32 parameter slots, draw/script bindings,
  defaults, texture units and wrapper ABI for each implementation.
- `equivalences.json`: verified source identities tied to a normalized Cg and
  parameter-layout SHA-256 signature. Different formulas, alpha handling or
  layouts remain distinct. This conservative check may miss algebraically
  equivalent programs; it never guesses from filenames.
- `fallbacks.json`: approximate same-function fallback paths, separate from
  equivalence proofs.

Runtime order: exact built-in, valid game/shared cache, verified equivalent,
optional conversion/compilation, then mapped approximate fallback. Existing
launcher conversion and compilation switches remain off by default. Enabling
both permits a supported distinct source to create its own `.cg`,
`.conversion.json`, `.gxp` and `.hash` cache files. A second load uses cache;
changed source or compiler identity invalidates the corresponding cache.
First compilation is synchronous and may pause loading. Unsupported vertex or
background-buffer semantics are rejected rather than silently translated.

Run `python scripts/bundle-artemis-shaders.py` after changing metadata. It rejects
stale equivalence signatures. To evaluate a candidate catalog outside the repo:

```text
python scripts/shader-effect-schema.py --candidates /private/candidate-effects --output /private/equivalences.json
```

Review the resulting table before replacing the public equivalence table. The
candidate directory contains `catalog.json` plus translated Cg/AGX1 files, using
the same schema as the canonical catalog. No candidate paths are exported.
