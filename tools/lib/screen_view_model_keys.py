#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""No destination is asked for two view-model classes under its route key.

Authority boundary: how the Compose screens *key* the view models they ask
`screenViewModel` for. It says nothing about what a view model does or which
destination a screen should use.

Why it exists. `screenViewModel(destination)` in `:core:ui` asks the
destination's entry for a view model under the destination's route. A
view-model store holds one model per key, and when a model of another class is
asked for under a key it already holds, androidx builds the new one, stores it,
and clears the old one. Nothing fails: the screen that asked second gets a
working model, and the screen that asked first keeps a reference to a model
whose scope has been cancelled, so its state stops moving and nothing says so.

That is how the Assistant pill in the browser's chrome stayed on "Taffy is
planning…" with Take over offered while the Ask sheet above it showed the
answer as done. The pill, the start page's task panel, the wait card and the
Ask conversation share one frame, `TaffyDestination.AssistantBar()`, so that
they share one bar model; the conversation panel also asked for its own
conversation model under that frame's route, and every time it did, it evicted
the model the pill was drawing. A second model on one destination takes a key
of its own through `viewModel(viewModelStoreOwner =
rememberScreenViewModelStoreOwner(destination, key), key = key)`, as the report
and conversation models there now do.

What it reads: every `screenViewModel(...)` call below the two Kotlin trees'
`src/main`, with the view-model class it is declared as. The destination is
resolved as written: a file-level value initialised from a `TaffyDestination`
expression is keyed by that expression, so two files that each name the bare
bar resolve to one route; `TaffyDestination.X` is keyed by itself; anything
else (a `destination` parameter) is keyed per file, because a parameter's
route is only known at run time and two calls in one file are the case that
can meet. A destination built with arguments inside the call,
`screenViewModel(TaffyDestination.X(id))`, is not matched and so not checked;
no call is written that way today, and the count this prints is the number it
did read.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import defaultdict

TREES = ("taffy-core/ui/android", "taffy-core/app/android")

TYPED_CALL = re.compile(
    r":\s*([A-Z]\w*)\s*=\s*screenViewModel\(\s*([\w.]+(?:\(\s*\))?)\s*\)"
)
GENERIC_CALL = re.compile(r"screenViewModel<\s*([A-Z]\w*)\s*>\(\s*([\w.]+(?:\(\s*\))?)\s*\)")
FRAME = re.compile(
    r"^(?:private\s+|internal\s+)?val\s+(\w+)\s*(?::\s*[\w.]+\s*)?=\s*"
    r"(TaffyDestination\.[\w.]+(?:\(\s*\))?)\s*$",
    re.M,
)


def calls_in(name: str, text: str) -> list[tuple[str, str]]:
    """(route, view-model class) for every call in one file's [text]."""
    frames = {m.group(1): re.sub(r"\s+", "", m.group(2)) for m in FRAME.finditer(text)}
    found = []
    for pattern in (TYPED_CALL, GENERIC_CALL):
        for match in pattern.finditer(text):
            cls, arg = match.group(1), re.sub(r"\s+", "", match.group(2))
            if arg in frames:
                route = frames[arg]
            elif arg.startswith("TaffyDestination."):
                route = arg
            else:
                route = f"{arg}@{name}"
            found.append((route, cls))
    return found


def collisions(sources: dict[str, str]) -> tuple[dict[str, dict[str, set[str]]], int]:
    """Routes asked for more than one class, each class with the files asking."""
    by_route: dict[str, dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))
    count = 0
    for name, text in sources.items():
        for route, cls in calls_in(name, text):
            by_route[route][cls].add(name)
            count += 1
    shared = {route: classes for route, classes in by_route.items() if len(classes) > 1}
    return shared, count


def kotlin_sources(root: str) -> dict[str, str]:
    sources = {}
    for tree in TREES:
        for base, dirs, files in os.walk(os.path.join(root, tree)):
            dirs[:] = [d for d in dirs if d not in ("build", ".gradle")]
            rel = os.path.relpath(base, root)
            if f"{os.sep}src{os.sep}main" not in f"{os.sep}{rel}{os.sep}":
                continue
            for file in files:
                if file.endswith(".kt"):
                    path = os.path.join(base, file)
                    with open(path, encoding="utf-8") as handle:
                        sources[os.path.relpath(path, root)] = handle.read()
    return sources


def self_test() -> list[str]:
    failures = []
    pill = "internal val CollapsedBar = TaffyDestination.AssistantBar()\n" \
        "val viewModel: AssistantBarViewModel = screenViewModel(CollapsedBar)\n"
    panel = "private val ConversationFrame = TaffyDestination.AssistantBar()\n" \
        "val barModel: AssistantBarViewModel = screenViewModel(ConversationFrame)\n" \
        "val conversationModel: AskConversationViewModel =\n" \
        "    screenViewModel(ConversationFrame)\n"
    shared, _ = collisions({"Pill.kt": pill, "Panel.kt": panel})
    classes = shared.get("TaffyDestination.AssistantBar()", {})
    if set(classes) != {"AssistantBarViewModel", "AskConversationViewModel"}:
        failures.append(f"the evicting pair across two files was not found: {dict(shared)!r}")
    fixed = panel.replace(
        "    screenViewModel(ConversationFrame)\n",
        "    viewModel(viewModelStoreOwner = rememberScreenViewModelStoreOwner("
        "ConversationFrame, KEY), key = KEY)\n",
    )
    shared, _ = collisions({"Pill.kt": pill, "Panel.kt": fixed})
    if shared:
        failures.append(f"a model under its own key was reported: {dict(shared)!r}")
    params = "val a: AViewModel = screenViewModel(destination)\n"
    shared, _ = collisions({"A.kt": params, "B.kt": params.replace("AView", "BView")})
    if shared:
        failures.append(f"parameters in two files were joined: {dict(shared)!r}")
    one_file = params + "val b = screenViewModel<BViewModel>(destination)\n"
    shared, count = collisions({"A.kt": one_file})
    if "destination@A.kt" not in shared or count != 2:
        failures.append(f"two classes on one parameter were missed: {dict(shared)!r}")
    return failures


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("root", nargs="?", default=".", help="repository root")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    failures = self_test()
    for failure in failures:
        print(f"screen view-model keys self-test: {failure}", file=sys.stderr)
    if failures:
        return 1
    if args.self_test:
        print("screen view-model keys self-test: fixtures passed")
        return 0
    shared, count = collisions(kotlin_sources(os.path.abspath(args.root)))
    for route, classes in sorted(shared.items()):
        asked = "; ".join(f"{cls} ({', '.join(sorted(files))})" for cls, files in sorted(classes.items()))
        print(f"screen view-model keys: {route} is asked for {asked}", file=sys.stderr)
    if shared:
        return 1
    print(f"screen view-model keys: {count} screenViewModel calls, one class per route")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
