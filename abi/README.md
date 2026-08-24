# SeqPro ABI baselines

The `v0.2.0` directory freezes the first public ABI for the same x86_64 GCC 13/libstdc++ toolchain
used by the package-and-ABI CI job:

- `seqpro.abi`: core `libseqpro.so.0.2.0`;
- `sequence_text.abi`: optional `libseqpro_sequence_text.so.0.2.0`.

The descriptions contain only exported interfaces, omit parameter names and absolute build paths,
and use hash-based type IDs. CMake's `SEQPRO_ENABLE_ABI_CHECKS=ON` option compares a DWARF-enabled
shared build with these files through `abidiff`.

Generate a baseline only for an intentional public ABI freeze:

```bash
scripts/generate_abi_baseline.sh \
  /usr/bin/abidw \
  build/libseqpro.so.0.2.0 \
  build/extensions/sequence_text/libseqpro_sequence_text.so.0.2.0
```

Do not regenerate a baseline merely to make a compatibility failure disappear. First inspect the
`abidiff` report and determine whether the change is compatible, should be redesigned, or requires
a new minor version and SONAME.
