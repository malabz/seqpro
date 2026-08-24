# Security policy

## Supported versions

The first public release line is SeqPro 0.2.x. Security fixes are provided for the latest 0.2.x
patch release while that line is supported. Unreleased development snapshots and the internal
0.1.0 milestone do not receive separate security backports.

| Version | Supported |
|---|---:|
| latest 0.2.x | Yes |
| earlier 0.2.x | Upgrade to the latest patch |
| 0.1.x and older | No public release |

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability. Use GitHub's
[private vulnerability reporting](https://github.com/malabz/seqpro/security/advisories/new) to
provide:

- the affected SeqPro version or commit;
- compiler, C++ runtime, platform, and build mode;
- the smallest reproducible FASTA, FAI, metadata, or operation sequence;
- the observed impact and any sanitizer diagnostics;
- whether the report includes sensitive or embargoed data.

The maintainers will acknowledge a complete report, validate the supported-version impact, and
coordinate a fix and disclosure. Please avoid publishing crash inputs or exploit details until a
coordinated disclosure date is agreed.

SeqPro parses local sequence and index files and exposes mapped views. Applications must still
enforce their own trust boundaries, file permissions, resource limits, and safe file-replacement
procedures.
