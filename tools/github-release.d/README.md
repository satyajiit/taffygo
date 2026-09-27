# `tools/github-release.d/`

What [`./tools/github-release`](../github-release) reads and runs. The command
is the entry point and `--help` lists its options; decision
0252
is the authority for what a GitHub release must be, and section 13 of the
release runbook
is the procedure around it.

| File | What it is |
|---|---|
| `version-floor` | The last version code Google Play accepted. `prepare` refuses an APK or bundle at or below it. Raised by hand when Play accepts an upload |
| `facts.py` | Reads the output of `apksigner`, `aapt2`, `keytool` and `bundletool`, and holds the rules an artifact must meet. It runs no tool, reads no key and reaches no network |
| `prepare.py` | Runs those tools, judges what they print with `facts.py`, and writes the release files only when every check passed. `check-prepared` re-reads them for `draft` |
| `facts_selftest.py` | The rules against recorded tool output, so they are tested on a host with no Android SDK |
| `selftest.sh` | `./tools/github-release self-test`: builds throwaway signed APKs and bundles, runs `prepare` over each, and drives `draft` and `publish` against a stand-in for `gh` |
| `notes/` | One release-notes body per version, `notes/X.Y.Z.md`, written with the release. `prepare` refuses without it and appends the section that tells a reader how to check the download |

The signing key never comes from here. `prepare` resolves it the way
`./tools/chromium/build` does, from the environment or from an untracked env
file, and passes the password to `keytool` through the environment only.
