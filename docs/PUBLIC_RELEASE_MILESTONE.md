# FlyArena — Public Release Milestone

This document is an **additive final milestone**. It does not replace or modify any previously read FlyArena handoff/specification documents.

Implement this only after the current gameplay, learning, customization, Training/Battle modes, drag-and-drop, fly creation, and related UI are stable.

## Goal

Publish FlyArena as a source-available Windows project so that:

- developers can clone and build it from source,
- ordinary users can download a ready-to-run Windows x64 Release ZIP,
- BANC and all third-party data/code are correctly attributed,
- releases are reproducible and versioned,
- private development files, user training files, secrets, and local telemetry are not published.

---

## 1. Repository cleanup before public release

Before making the GitHub repository public:

- remove hardcoded developer-specific absolute paths such as `D:\...` or `C:\Users\...`,
- replace them with repository-relative or configurable paths,
- remove generated build output,
- remove private telemetry/results,
- remove local `.flytrain` files,
- remove temporary test artifacts,
- verify that no API keys, tokens, passwords, credentials, or other secrets are tracked,
- inspect Git history for accidentally committed sensitive data.

Create/update `.gitignore` for at least:

```gitignore
.vs/
bin/
obj/
results/

*.pdb
*.ilk
*.log

data/cache/
data/io/
data/training/*.flytrain
```

Do not ignore intentionally committed tiny test fixtures.

---

## 2. Source-available license

Root `LICENSE` contains FASL-1.0. It permits reading, personal/educational and
non-commercial research use, and private modification. Public derivative works
and commercial use require the Licensor's prior written permission. Do not
describe FlyArena as open source.

Before publication, replace the generic rights-holder label with the chosen
legal name or organization and confirm a permission-request contact path.

Also create:

- `THIRD_PARTY_NOTICES.md`
- `DATA_LICENSES.md`

These should distinguish:

- FlyArena source-code license,
- BANC dataset license and attribution,
- third-party library obligations,
- bundled asset/skin licenses.

---

## 3. BANC data distribution

Do not commit the complete BANC dataset/cache into the normal Git repository unless explicitly justified.

Preferred flow:

1. Ship FlyArena source/binaries without the large BANC cache.
2. Detect missing data on startup.
3. Present a clear setup / `Download Data` workflow.
4. Download only from known official/approved sources.
5. Record source, version, and checksum.
6. Generate FlyArena cache locally.
7. Show a useful error/setup screen when required data is unavailable.

Documentation must state:

- BANC version,
- source,
- DOI/citation,
- license,
- required files,
- approximate download/storage requirements.

---

## 4. Root README

Create a polished public-facing `README.md`.

Recommended order:

1. FlyArena title and one-sentence summary
2. Hero screenshot or short GIF
3. What FlyArena is
4. Main features
5. Download latest release
6. Quick start
7. Training mode
8. Battle mode
9. Fly creation and equipment customization
10. `.flypack` / `.flytrain`
11. Building from source
12. BANC/data setup
13. Scientific/modeling caveats
14. Contributing
15. Citation
16. License

Clearly distinguish:

- measured BANC anatomy,
- inferred physiology,
- gameplay/model assumptions.

Do not claim FlyArena is a complete biological simulation of a living fruit fly.

---

## 5. End-user Windows release package

Ordinary users should not require Visual Studio.

Produce a portable package similar to:

```text
FlyArena-v1.0.0-Windows-x64/
├─ FlyArena.exe
├─ assets/
├─ configs/
├─ LICENSE.txt
├─ THIRD_PARTY_NOTICES.txt
├─ DATA_LICENSES.txt
├─ README.txt
└─ data/
   └─ README_DATA.txt
```

Do not include:

- developer `.flytrain` files,
- private/user telemetry,
- compiler intermediates,
- repository metadata,
- secrets.

Release assets should include at least:

```text
FlyArena-vX.Y.Z-Windows-x64.zip
SHA256SUMS.txt
```

---

## 6. Versioning

Use semantic-style public versions:

- `v1.0.0` — first stable public release
- `v1.0.1` — bug fix
- `v1.1.0` — backwards-compatible feature release
- `v2.0.0` — incompatible public format/API change

Version and validate compatibility for at least:

- application version,
- `.flypack` format,
- `.flytrain` format,
- learner/feature schema,
- BANC compatibility,
- IO-map compatibility.

Never silently reinterpret an old save/package format.

---

## 7. GitHub Actions CI

Create Windows CI under `.github/workflows/`.

For pushes and pull requests:

1. checkout repository,
2. configure MSVC/Visual Studio build environment,
3. build FlyArena,
4. run portable/unit/behavior tests,
5. fail CI on build/test failure,
6. optionally upload a CI build artifact.

Do not require the full BANC dataset for ordinary lightweight CI if this can be avoided. Use small fixtures or tests for logic that does not require the complete dataset.

---

## 8. Automated GitHub Releases

Create a release workflow triggered by a version tag such as:

```text
v1.0.0
```

Desired flow:

```text
push version tag
    ↓
Windows build
    ↓
tests
    ↓
portable package
    ↓
ZIP
    ↓
SHA-256 checksums
    ↓
GitHub Release
```

The GitHub Release should contain:

- packaged Windows x64 ZIP,
- checksum file,
- generated or curated release notes.

Do not automatically publish a release if build/tests fail.

---

## 9. Citation

Add `CITATION.cff`.

Include appropriate citation information for FlyArena itself.

Also document how users should cite the BANC dataset/paper separately.

Keep FlyArena authorship and BANC authorship clearly separated.

---

## 10. Contributing / issue templates

Add at least:

- `CONTRIBUTING.md`
- bug report issue template
- feature request issue template

`CONTRIBUTING.md` should describe:

- how to build,
- how to run tests,
- coding expectations,
- architecture guardrails,
- how to submit pull requests,
- scientific honesty requirements for biology-related claims.

---

## 11. Public release validation checklist

Before `v1.0.0`:

- [ ] clean clone builds successfully
- [ ] CI passes
- [ ] packaged build launches on a clean Windows machine/VM
- [ ] missing BANC data produces a useful setup flow
- [ ] Training works
- [ ] Battle works
- [ ] `.flypack` import/export works
- [ ] `.flytrain` persistence works
- [ ] Battle never mutates training state
- [ ] random Trainer mode works
- [ ] training speed controls work
- [ ] no personal paths remain
- [ ] no secrets are present
- [x] FASL-1.0 source-available license added
- [ ] legal rights-holder name/contact confirmed
- [ ] BANC attribution included
- [ ] third-party notices included
- [ ] README complete
- [ ] release ZIP contains no developer/private data
- [ ] checksums generated
- [ ] version/tag/release notes agree

---

## 12. Final intended user experience

The end result should allow a non-developer to:

```text
Open GitHub
→ Releases
→ Download FlyArena-vX.Y.Z-Windows-x64.zip
→ Extract
→ Run FlyArena.exe
→ Complete BANC data setup if necessary
→ Create a fly
→ Customize wings / sword / shield / skins
→ Train it
→ Save/share .flypack and .flytrain
→ Load another person's fly
→ Battle
```

The source repository should remain useful to developers and researchers, while the GitHub Release should be usable by ordinary players without requiring a compiler. The public repository and source ZIP contain only the current release surface, not historical experiment files.

## Implementation order

Treat this as the **final milestone**, after the currently planned feature work is complete.

Do not interrupt unfinished Training/Battle/customization work merely to implement publication infrastructure early.
