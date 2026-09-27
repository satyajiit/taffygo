# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Project the browser's enabled catalog into the public website directory."""

import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "taffy-core/components/intelligence/core/rust/model-router/catalog/baseline.json"
TARGET = ROOT / "website/lib/provider-directory.json"


def render() -> str:
    catalog = json.loads(SOURCE.read_text())
    providers = []
    for provider in catalog["providers"]:
        if not provider["enabled"]:
            continue
        models = [
            {"id": model["model_id"], "name": model["display_name"],
             "vision": "IMAGE" in model["input_modalities"],
             "reasoning": model["reasoning"], "tools": model["tool_calling"],
             "context": model["context_window"], "enabled": model["enabled"]}
            for model in catalog["models"]
            if model["provider_id"] == provider["provider_id"]
        ]
        providers.append({"id": provider["provider_id"], "name": provider["display_name"],
                          "listing": provider["model_source"] == "DYNAMIC_LISTING",
                          "models": models})
    return json.dumps({"version": catalog["catalog_version"], "providers": providers},
                      ensure_ascii=False, separators=(",", ":")) + "\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    expected = render()
    if args.check:
        if not TARGET.exists() or TARGET.read_text() != expected:
            raise SystemExit("Provider directory differs from the browser. Run python3 website/scripts/build_provider_directory.py")
        print("Provider directory matches the browser catalog.")
    else:
        TARGET.write_text(expected)
