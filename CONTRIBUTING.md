# Contributing to TaffyGo

Pull requests, bug reports and questions are all welcome.

For a security problem, do not open an issue or a pull request. Follow
[SECURITY.md](SECURITY.md) instead.

## Licence and sign-off

TaffyGo's own source is published under the Mozilla Public License 2.0
([LICENSE](https://github.com/satyajiit/taffygo/blob/main/LICENSE)). You
contribute under the same licence and keep your copyright. There is no
contributor licence agreement, and nothing here asks you to assign anything.
Because every contributor keeps their copyright, relicensing the project would
need each of their consent, and that is intended.

Every commit carries a `Signed-off-by:` line. It certifies the
[Developer Certificate of Origin 1.1](https://developercertificate.org/): you
wrote the change, or you have the right to submit it under the project's
licence. `git commit -s` adds the line from your Git name and email. If you
forgot, `git commit --amend -s` fixes the last commit and
`git rebase --signoff main` fixes a branch.

The TaffyGo name, the marks, the Taffy character and the artwork are reserved
separately ([TRADEMARKS.md](https://github.com/satyajiit/taffygo/blob/main/TRADEMARKS.md)).
A pull request that adds artwork should say where it came from.

## Before you start

Small fixes can go straight to a pull request. For a new feature, a new
dependency, a change to a generated contract, or anything that touches how
Taffy is allowed to act, open an issue or a
[discussion](https://github.com/satyajiit/taffygo/discussions) first. The
project keeps its design records in the maintainer's working tree, which is
not published, and code comments cite them by number ("decision 0086"). Asking
first saves you from building against a decision you could not see.

Third-party dependencies are rare on purpose. Every Rust crate that ships has
to clear Chromium's review for vendored code, so a runtime dependency is the
exception and test-only machinery goes in dev-dependencies.

[AGENTS.md](AGENTS.md) is the engineering guide: the layout, the commands, the
code style, and the mistakes that no check catches. Read the part about the
area you are changing.

## Set up

```bash
./tools/doctor                          # read-only: what this host can build and check
./tools/bootstrap --profile android     # the host loops: pinned Rust, pnpm, Gradle, Android SDK check
./tools/bootstrap --profile chromium --workspace /srv/chromium-taffy
                                        # the full browser build, x86-64 Linux only; the workspace
                                        # holds the Chromium checkout and needs about 400 GB free
```

Every command takes `--help`, prints what it resolved, and explains how to fix
a failure.

## Run the checks

No hosted service runs the checks. You run them before you open a pull
request, and the template asks what they printed:

```bash
./tools/check docs    # Markdown: vocabulary, status labels, links
./tools/check fast    # every lane this host can run
```

A lane whose toolchain is missing skips and names the reason. A skip is not a
pass, so list the lanes that skipped in your pull request. `./tools/check`
with no lane name prints its usage and checks nothing.

The loops for each part of the tree, from the repository root:

| Part | Commands |
|---|---|
| Rust | `cargo fmt --all -- --check`, `cargo clippy --workspace --all-targets --all-features -- -D warnings`, `cargo test --workspace --locked` |
| Contracts | `./tools/check fast --only contracts` runs every generator's `--check`, `--self-test` and `--verify` |
| Compose UI | `./gradlew --no-daemon lintDebug testDebugUnitTest` |
| Website | `pnpm --dir website lint`, then `typecheck`, then `test`, one script per command |
| Browser (C++, GN, anything that ships) | `./tools/chromium/build --profile dev-arm64`, then `./tools/chromium/test --profile dev-arm64 taffy_unittests` |

`./tools/check fast` compiles no C++, and Cargo never compiles the Rust bridge
that GN builds, so a change to either needs the Chromium build. If you could
not run it, say so in the pull request. For a change to what a person sees,
attach before and after screenshots from a phone or an emulator.

## Write the commit

- One logical change per commit, with the reason in the body.
- The subject reads `area: what changed`, for example
  `filtering: count blocked requests per tab`. If the change follows a decision
  that a nearby comment cites, cite the same number.
- Sign off with `-s`.
- Leave AI tools out of the history. Do not add a `Co-Authored-By:` trailer
  that names an assistant, or a "Generated with" footer. The commit author is
  the person accountable for the change.

## The three rules

1. **One product line.** TaffyGo is 1.0 and the updates that follow it. Do not
   add editions, tiers or labels for early builds, in code, copy or
   documentation. `./tools/check docs` refuses the usual words; the list is
   `BANNED_RULES` in
   [tools/lib/docs_lint.py](https://github.com/satyajiit/taffygo/blob/main/tools/lib/docs_lint.py).
2. **One assistant.** Taffy is the only AI actor in the product. A feature that
   looks like it needs a second assistant is a Personality setting or a skill
   for Taffy.
3. **Plain language.** Anything a person reads uses the product's own words:
   task, workspace, sources, Library, Memory, Taffy's tabs, Take over, Hand
   back. Name who acts ("Taffy will open 4 tabs"), say exactly what leaves the
   phone, and never round partial work up to success ("Partly done, 3 pages
   read"). Every visible string lives in a resource file, and
   `python3 taffy-core/resources/catalog/tools/check_strings.py` checks that
   on any host.

## How a merged pull request reaches a release

This repository is generated. The maintainer works in a private tree that also
holds the design records, and an export produces this repository from it.
When your pull request is merged here, the maintainer applies it to that tree
as a patch with `git am`, which keeps you as the author and keeps your
sign-off. The next export carries it back here. The export refuses to replace
a commit in this repository that it does not reproduce, so a merged change
cannot be dropped by accident in between. Releases are built from that tree,
so your change ships in the first release after it is imported.

## Other ways to help

- Report a bug or ask for a feature with the
  [issue forms](https://github.com/satyajiit/taffygo/issues/new/choose).
- Try TaffyGo on a phone model nobody has reported yet, and post how it went
  in [Discussions](https://github.com/satyajiit/taffygo/discussions).
- Answer someone else's question there.

Everyone taking part follows the [code of conduct](CODE_OF_CONDUCT.md).
