# Release-candidate and release procedure

SeqPro separates release-candidate validation from publishing. The release-candidate workflow
builds evidence and artifacts but never creates a tag or GitHub Release.

## Compatibility contract

- The root `project(SeqPro VERSION ...)` declaration is the single version source.
- Generated `seqpro/version.h`, the CLI, package config, and library filenames must agree.
- 0.2.x patches use SONAME `0.2` and CMake `SameMinorVersion` compatibility.
- ABI checks compare libraries built with the same fixed GCC/libstdc++ toolchain.
- Compatibility is not claimed across libc++, libstdc++ ABI modes, architectures, or minor
  versions.
- The core FAI and metadata schema 1 remain independent from SequenceText.

## Prepare a release candidate

1. Start from a clean commit on the intended release branch.
2. Confirm `CHANGELOG.md` has one `0.2.0 - Unreleased` entry and final public names.
3. Run the `Quality` workflow and resolve every matrix, sanitizer, analysis, documentation,
   package, symbol, and ABI failure.
4. Run the manual `Fuzz` workflow. Each target must complete at least 300 seconds without a crash,
   timeout, unexpected exception, or sanitizer finding.
5. Run the manual `Release candidate` workflow for the exact commit.
6. Download and retain the workflow summary, fuzz corpora, API documentation, source archive,
   checksum, and manifest.
7. Confirm the repository still has no release tag or GitHub Release created by automation.

Local archive generation uses:

```bash
scripts/build_release_candidate.sh . release-artifacts <commit>
scripts/validate_release_archive.sh \
  release-artifacts/seqpro-0.2.0-rc.<short-commit>.tar.gz
```

The builder requires a clean worktree, archives the explicit commit twice with a fixed prefix and
`gzip -n`, compares the bytes, and writes SHA-256 and JSON manifest files. It never archives the
working directory.

## Required release-candidate evidence

- GCC 9.5 and Clang 10 minimum-toolchain jobs.
- Modern GCC and Clang Debug/Release, static/shared, SequenceText OFF/ON matrix.
- C++17 and C++20 add_subdirectory, offline FetchContent, and installed-package consumers.
- ASan, UBSan, LSan, and TSan.
- clang-tidy, GCC `-fanalyzer`, source-convention, clang-format, actionlint, and ShellCheck gates.
- Samtools 1.24 bidirectional interoperability.
- Strict Doxygen output with no undocumented public symbols.
- SONAME, dynamic dependency, symbol allowlist, and cross-DSO exception tests.
- Frozen source consumer and libabigail ABI comparison.
- Reproducible source archive rebuilt twice with identical SHA-256.
- Successful builds after extracting under a path containing spaces, installing, relocating the
  prefix, and consuming both core-only static and SequenceText-enabled shared packages.

The GCC analyzer build keeps all analyzer diagnostics fatal except
`-Wanalyzer-malloc-leak` and `-Wanalyzer-use-of-uninitialized-value`. GCC 13 and GCC 14 both report
those two diagnostics inside libstdc++ implementations of `std::unordered_map` and `std::string`
for valid SeqPro code. ASan, LSan, UBSan, clang-tidy, normal warnings, and the remaining analyzer
diagnostics continue to cover owned code; do not add another suppression without a documented,
reproducible system-header false positive.

## ABI baseline

`abi/v0.2.0/seqpro.abi` and `abi/v0.2.0/sequence_text.abi` are generated from DWARF-enabled
shared libraries using the pinned ABI workflow toolchain. Do not regenerate a baseline merely to
make a failing diff disappear.

For a compatible 0.2.x change:

1. inspect the `abidiff` report;
2. preserve public layout, exported functions, exceptions, and vtables;
3. update tests and changelog without changing the baseline.

For an intentional incompatible release:

1. choose a new minor version;
2. update the SONAME through the project version;
3. update source consumers and compatibility documentation;
4. generate and review a new ABI baseline.

## Publish the final 0.2.0 release

Publishing is a separate, explicitly authorized operation:

1. replace `Unreleased` with the release date;
2. add a new top-level `Unreleased` section;
3. rerun the complete release-candidate workflow on that commit;
4. create an annotated `v0.2.0` tag;
5. verify the tag points to the validated commit;
6. create a GitHub Release using `docs/releases/v0.2.0.md`;
7. upload the source archive, checksum, manifest, and API documentation generated from that commit;
8. verify public checksums and install from the downloaded archive.

Do not publish generic precompiled Linux binaries. Their glibc, libstdc++, compiler ABI, and build
configuration would imply compatibility that SeqPro does not promise.
