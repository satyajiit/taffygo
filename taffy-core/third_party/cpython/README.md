# `taffy-core/third_party/cpython/`

The vendoring specification for TaffyGo's Python runtime, and the tools that
build both halves of it: the interpreter that ships inside the installer and
the library artifacts the asset catalog names. **No CPython source is committed
here.** [`README.chromium`](README.chromium) and
[`tools/manifest.json`](tools/manifest.json) pin the release and its digest, and
`tools/build_interpreter.py` cross-builds that release with the Chromium
checkout's own toolchain — a source tree of this size is a checkout-time
dependency, and a second committed copy of it would be the kind of duplicate
this repository treats as a defect.

Decision
`0046-a-vendored-python-runtime-ships-its-interpreter-and-fetches-its-library.md`
is the authority for the split this directory implements. In one line: the
interpreter ships inside the installer and the library is fetched, because
Play's restriction on downloaded code names executables and exempts what an
interpreter interprets — and because this browser could not load a downloaded
native library even where the store allowed it.

## What lives here

| Path | What it is |
|---|---|
| `README.chromium` | The provenance record. Names the release, its licence, and its shipping state |
| `BUILD.gn` | Compiles the pinned CPython sources with committed per-ABI configuration; it never runs `configure` and never links a dropped archive |
| `generated/` | Version-roll outputs: the two ABI headers, reviewed built-in table, frozen startup modules, and their digest record |
| `tools/manifest.json` | The version pin `TOOLCHAIN.md` indexes, with the release's own SHA-256 and the module set the sandbox permits |
| `tools/build_interpreter.py` | Cross-builds the interpreter for one Android ABI with the Chromium checkout's clang and the NDK sysroot, then measures it |
| `tools/artifact_zip.py` | The one deterministic archive shape. Two builds of the same tree produce the same bytes, which is what lets a catalog row name a SHA-256 |
| `tools/build_stdlib.py` | Packages a CPython `Lib/` directory into the `python-stdlib` asset and prints the facts a published catalog variant needs |
| `tools/build_packages.py` | Packages the allowlisted pure-Python set into the `python-toolkit` asset |
| `tools/check_bootstrap.py` | Drives the bootstrap below over synthetic archives in a child interpreter whose `sys.path` is empty |
| `tools/freeze_modules.py` | Marshals the bootstrap and CPython's startup set into the frozen table that lets an interpreter boot with no path at all |
| `runtime/taffy_stdlib_boot.py` | The bootstrap itself: a meta-path finder over a memory-mapped zip, so a worker with no filesystem still has a standard library |
| `runtime/taffy_python_main.c` | The embedder: frozen table, empty search path, library from a descriptor. The shape the utility service will have |
| `packages/allowlist.txt` | Every package a bundled entrypoint may import beside the standard library. Empty on purpose — see below |

Each tool has a `--self-test` that drives it over synthetic input, refusals
included, and the `catalog` lane of `./tools/check fast` runs every one.

## The two assets, and why they are two

`python-stdlib` is required: a profile fetches it when it starts, because the
start page cannot open until a Python worker can run. `python-toolkit` is on demand: most
sessions never run an entrypoint that needs a package, and a phone should not
carry what it does not use. They also change on different clocks — the library
moves when CPython does, the toolkit when an entrypoint needs something — and
an asset is the unit that changes together.

Both are `zip` containers that stay zips. Nothing is unpacked, so an artifact is
one file to verify and one descriptor to hand a sandboxed worker, which is what
decision 0041 requires of every resource a worker receives.

## The package allowlist is empty, and that is the state

The compiled registry serves `document.build` and `spreadsheet.build`; both use
only the descriptor-backed standard library. No third-party package therefore
has an argued reason to be importable. An allowlist seeded with plausible
entries is an allowlist nobody reviewed, and `build_packages.py` refuses to
produce an empty toolkit artifact.

## The interpreter, and what building it settled

`tools/build_interpreter.py` cross-builds one Android ABI and refuses to report
a size for a build that broke any of three rules. Each rule exists because
breaking it is invisible until a device or an import fails:

- **Every extension module is compiled in.** A sandboxed utility process cannot
  `dlopen`, so an extension that landed as a `.so` is a module that is simply
  missing at run time. `MODULE_BUILDTYPE=static` is what makes this true and
  the tool counts `.so` files afterwards rather than trusting the flag.
- **Every loadable segment aligns to 16 KiB.** A device with a 16 KiB page size
  refuses a binary aligned for 4 KiB, and the emulator most builds are tested on
  has the smaller page.
- **`_ctypes` and `_socket` are configured out.** The first is arbitrary native
  calls and the second is network; the sandbox has neither, so shipping them
  would put a capability in the process the sandbox exists to deny.

`runtime/taffy_stdlib_boot.py` is the other half. The worker never learns where
the library is: it maps the descriptor the browser hands it and imports through
a meta-path finder over that mapping, with `sys.path` empty for the life of the
process. `tools/check_bootstrap.py` drives it in a child interpreter and
asserts the emptiness, because an import that quietly fell through to a
filesystem would otherwise look exactly like one the bootstrap served.

The bootstrap cannot be the first thing that runs, because CPython needs a
codec and a handful of pure-Python modules before it can execute anything at
all, and reads those from a path. `tools/freeze_modules.py` removes that path:
it marshals the bootstrap and that startup set into a C table an embedder
assigns to `PyImport_FrozenModules`, and refuses to marshal with an interpreter
that is not the pinned one — a code object in the wrong format fails on its
first frozen import with an error naming the module rather than the mismatch.
`runtime/taffy_python_main.c` is the smallest program that puts the two
together: frozen table joined onto CPython's own, an empty module search path,
and a standard library that arrives as a descriptor and is closed as soon as it
is mapped.

The measurements both tools produced, on which host and on which device, are in
`docs/development/verification-report.md`.

## What remains to verify

- **The M7 exit review.** The source-built GN target, typed utility worker,
  cancellation path and two fixed entrypoints now exist, but SP-07 remains the
  owner of device sandbox/escape, size and resource-limit evidence. A host or
  cross compile is not that evidence.
- **The product run.** Shipping Android profiles enable
  `taffy_enable_python_runtime`; the host-side browser test compiles a real
  browser-to-worker launch and validates a generated document. This change has
  not installed that build on a device, so no device execution claim is made.
- **Whether the library artifact carries source or byte code.**
  `build_stdlib.py` excludes `.pyc` today, with a reason. The measurement in
  the verification report argues the other way and neither number has been put
  to the decision that owns it.
- **Where the desktop variants are published.** The two Android ones are
  answered and are no longer open. The delivery origin is a build argument
  (`taffy_asset_origin`, `taffy-core/browser/asset_delivery_configuration.gni`)
  naming a provisioned host, and on 2026-08-23 `./tools/assets publish`
  uploaded the standard-library artifact for both Android ABIs, fetched each
  object back from the origin the product fetches from, and compared the
  SHA-256 and the length before either catalog row moved to `published`. The
  round trip then ran to the end: the object downloaded from the CDN, pushed to
  the phone and given to the worker binary served 20 of 21 modules out of the
  mapping with `sys.path == []`, the same answer as the locally built copy. The
  verification report has
  the numbers and the cached-404 finding that came with them. What is still
  open is the four desktop variants, which stay `unpublished` because nothing
  has built an interpreter for them; a catalog row on an unpublished variant is
  refused before a request is made, which is the same refusal as before and for
  a better reason.

## Adding an artifact

The catalog is the place, not this directory. Add a row to
`taffy-core/components/delivery/core/rust/asset-plane/catalog/source/assets.json`
and run its generator; a row of a kind that already exists needs no Rust, no
C++, no Kotlin and no string. When the bytes have been built and uploaded,
change the variant from `unpublished` to `published` and paste the four facts
these tools print.
