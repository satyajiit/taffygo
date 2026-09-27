#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""What the import must refuse, proved against rows written to be refused.

Run as `import_pi_catalog.py --self-test`, and by the `catalog` lane, which is
the point: the import itself reaches the network and no gate may run it, so
without this the only thing ever checked about the mapping is whatever the last
snapshot happened to contain. A refusal that stops firing is invisible in that
arrangement — the rows it should have stopped simply appear, correctly shaped
and wrong.

Every case here is one that has actually gone wrong. The plan-backed zero and
the rung map on a model that does not think were both written into the source
by an earlier pass of this tool and caught downstream by `embedded_baseline.rs`
in Rust, which is two layers further out than the place that made the mistake.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pi_catalog_register import Imported  # noqa: E402


def _row(**overrides):
    row = {
        "id": "fixture-model",
        "name": "Fixture Model",
        "api": "openai-completions",
        "provider": "fixture",
        "baseUrl": "https://api.fixture.test/v1",
        "reasoning": True,
        "input": ["text"],
        "cost": {"input": 1.0, "output": 2.9999999999999996,
                 "cacheRead": 0.10000000399999999, "cacheWrite": 0.5},
        "contextWindow": 200000,
        "maxTokens": 32000,
    }
    row.update(overrides)
    return row


_FIXTURE = Imported("fixture", "fixture", "Fixture",
                    "https://api.fixture.test", "OPEN_AI_COMPLETIONS")


def _cases(model_row):
    """Every case, as (name, checker). A checker raises to fail."""

    def shaped(row, plan_backed=False):
        return model_row(_row(**row), _FIXTURE, "2026-09-03", plan_backed)

    def float_noise_is_read_as_the_figure_the_vendor_published():
        built, reason = shaped({})
        assert reason is None, reason
        price = built["price"]
        assert price["usd_per_million_output"] == "3", price
        assert price["usd_per_million_cache_read"] == "0.1", price

    def a_family_with_no_adapter_is_refused():
        built, reason = shaped({"api": "bedrock-converse-stream"})
        assert built is None and "family" in reason, reason

    def a_base_url_that_substitutes_a_value_is_refused():
        built, reason = shaped(
            {"baseUrl": "https://api.fixture.test/{ACCOUNT}/v1"})
        assert built is None and "substitutes" in reason, reason

    def a_base_url_outside_the_recorded_origin_is_refused():
        built, reason = shaped({"baseUrl": "https://elsewhere.test/v1"})
        assert built is None and "beneath the origin" in reason, reason

    def a_row_with_no_rate_is_refused():
        built, reason = shaped({"cost": {"cacheRead": 1.0}})
        assert built is None and "no input or no output rate" in reason, reason

    def a_plan_backed_row_that_imputes_nothing_is_refused():
        built, reason = shaped({"cost": {"input": 0, "output": 0}},
                               plan_backed=True)
        assert built is None and "spend nothing" in reason, reason

    def a_plan_backed_row_says_its_price_was_imputed():
        built, reason = shaped({}, plan_backed=True)
        assert reason is None, reason
        assert built["price"]["basis"] == "IMPLIED", built["price"]

    def a_model_that_does_not_think_carries_no_rung_map():
        built, reason = shaped({
            "reasoning": False,
            "thinkingLevelMap": {"off": None, "high": "high"},
        })
        assert reason is None, reason
        assert "thinking_levels" not in built, built
        # And it is offered for both rungs, which is the standing rule.
        assert built["roles"] == ["FAST_BROWSING", "PRIMARY_REASONING"], built

    def a_thinking_model_keeps_the_unsupported_rungs_it_was_given():
        built, reason = shaped({"thinkingLevelMap": {"off": None,
                                                     "max": "max"}})
        assert reason is None, reason
        assert built["thinking_levels"] == {"OFF": None, "MAX": "max"}, built
        assert built["roles"] == ["PRIMARY_REASONING"], built

    def image_input_adds_the_vision_role_and_the_modality():
        built, reason = shaped({"input": ["text", "image"]})
        assert reason is None, reason
        assert "VISION" in built["roles"], built
        assert built["input_modalities"] == ["TEXT", "IMAGE"], built

    def an_allowance_larger_than_the_window_is_clamped_and_attributed():
        built, reason = shaped({"contextWindow": 1000, "maxTokens": 4000})
        assert reason is None, reason
        assert built["max_output_tokens"] == 1000, built
        chosen = built["provenance"]["taffygo_chosen"]
        assert "max_output_tokens" in chosen, chosen

    def every_row_states_that_it_calls_tools_and_says_why():
        built, reason = shaped({})
        assert reason is None, reason
        assert built["tool_calling"] is True, built
        assert "tool_call" in built["provenance"]["note"], built["provenance"]

    # The cases are the local functions defined above, in definition order.
    # `model_row` and `shaped` are named here because both are callable and
    # neither is a case — the first is this function's own argument.
    return [(name, checker) for name, checker in locals().items()
            if callable(checker) and not name.startswith("_")
            and name not in {"shaped", "model_row"}]


def run(model_row):
    """Every case, reported one per line. Returns a process exit code."""
    failures = 0
    for name, checker in _cases(model_row):
        try:
            checker()
        except AssertionError as error:
            failures += 1
            print(f"  fail  {name}: {error}")
        else:
            print(f"  ok    {name}")
    if failures:
        print(f"pi catalog import: {failures} self-test case(s) failed")
    else:
        print("pi catalog import: every refusal and every mapping holds")
    return 1 if failures else 0
