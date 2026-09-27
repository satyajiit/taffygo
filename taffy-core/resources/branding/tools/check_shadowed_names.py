#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Assert every shadowed upstream resource name still exists at the pin.

The :image_override_resources target and the branding strings replace Chromium
artwork and widget titles by resource *name*, with resource-overlay
precedence (decision 0019; patch specification 0002 records the mechanism
and its risk). The risk is silent: if upstream renames a resource, the
shadow stops matching, the Chromium art quietly returns to the product, and
nothing fails. This check converts that silence into a loud failure —
`./tools/chromium/build` runs it against the checkout before every build.

What is checked, from two sources of truth it does not restate:

  * every `role: override` asset in icons/manifest.json whose name does not
    begin with `taffygo_` (TaffyGo-owned names shadow nothing);
  * every committed file under java/res_images/ (the XML shadows);
  * the widget-title strings the branding strings file overrides, against
    the upstream file that defines them.

Exit status: 0 clean, 1 on any finding. Stdlib only.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

BRANDING_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(BRANDING_DIR, "icons", "manifest.json")
RES_IMAGES = os.path.join(BRANDING_DIR, "java", "res_images")
STRINGS = os.path.join(BRANDING_DIR, "java", "res", "values", "taffy_branding_strings.xml")

# Where upstream declares the resources the overlay shadows. Narrow on
# purpose: a name that moves outside these roots is a change worth a human
# look, not a silent widening of the search.
UPSTREAM_RES_ROOTS = (
    "chrome/android/java/res",
    "chrome/android/java/res_base",
    "chrome/android/java/res_chromium_base",
    "components/browser_ui/styles/android/java/res",
    "components/browser_ui/styles/android/java/res_app",
    "components/browser_ui/styles/android/java/res_chromium",
)

# The upstream definition site of the widget-title strings the branding
# strings file overrides beside app_name.
UPSTREAM_CHANNEL_CONSTANTS = "chrome/android/java/res_chromium_base/values/channel_constants.xml"


def shadowed_resources() -> set[tuple[str, str]]:
    """(resource type, resource name) pairs the overlay claims to shadow."""
    shadows: set[tuple[str, str]] = set()

    with open(MANIFEST, encoding="utf-8") as handle:
        manifest = json.load(handle)
    for asset in manifest.get("assets", []):
        if asset.get("role") != "override":
            continue
        # res/<type>-<qualifier>/<name>.<ext>
        parts = asset["output"].split("/")
        res_type = parts[1].split("-")[0]
        name = os.path.splitext(parts[2])[0]
        if name.startswith("taffygo_"):
            continue
        shadows.add((res_type, name))

    for path in glob.glob(os.path.join(RES_IMAGES, "*", "*")):
        res_type = os.path.basename(os.path.dirname(path)).split("-")[0]
        name = os.path.splitext(os.path.basename(path))[0]
        if not name.startswith("taffygo_"):
            shadows.add((res_type, name))

    return shadows


def overridden_strings() -> set[str]:
    with open(STRINGS, encoding="utf-8") as handle:
        names = set(re.findall(r'<string name="([a-z0-9_]+)"', handle.read()))
    # app_name is checked the same way the widget titles are: it is the
    # original shadow this mechanism was built for.
    return names


def upstream_has_resource(checkout: str, res_type: str, name: str) -> bool:
    for root in UPSTREAM_RES_ROOTS:
        pattern = os.path.join(checkout, root, f"{res_type}*", f"{name}.*")
        if glob.glob(pattern):
            return True
    return False


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--checkout", required=True, help="path to the Chromium src checkout"
    )
    args = parser.parse_args(argv)

    checkout = os.path.abspath(args.checkout)
    if not os.path.isdir(os.path.join(checkout, "chrome")):
        print(f"error: {checkout} does not look like a Chromium checkout", file=sys.stderr)
        return 1

    findings = 0

    for res_type, name in sorted(shadowed_resources()):
        if not upstream_has_resource(checkout, res_type, name):
            print(
                f"{res_type}/{name}: no upstream resource of this name at the "
                "pin. The shadow is dead — upstream renamed or removed it. "
                "Update icons/manifest.json or java/res_images/ (and the "
                "consumer audit in patch spec 0002) in the same change."
            )
            findings += 1

    constants = os.path.join(checkout, UPSTREAM_CHANNEL_CONSTANTS)
    try:
        with open(constants, encoding="utf-8") as handle:
            upstream_names = set(re.findall(r'<string name="([a-z0-9_]+)"', handle.read()))
    except FileNotFoundError:
        print(
            f"{UPSTREAM_CHANNEL_CONSTANTS}: missing at the pin. The string "
            "shadows cannot be verified; find where the channel constants "
            "moved and update this check."
        )
        findings += 1
        upstream_names = set()

    if upstream_names:
        for name in sorted(overridden_strings()):
            if name not in upstream_names:
                print(
                    f"string/{name}: not defined in {UPSTREAM_CHANNEL_CONSTANTS} "
                    "at the pin. The override no longer shadows anything — "
                    "upstream renamed it or moved it; follow it."
                )
                findings += 1

    if findings:
        print(f"\n{findings} dead shadow(s). A dead shadow ships Chromium branding.")
        return 1
    print("every shadowed resource and string still exists upstream at the pin")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
