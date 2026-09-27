#!/usr/bin/env python3
# Copyright (c) 2026 Matterward Labs Private Limited.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Which providers an import may write a row for, and which it may not.

Split from `import_pi_catalog.py` along its one seam: everything here is a
*register* — the closed vocabulary of wire families, the table of providers
this catalog will import, and the reason beside every provider in the source it
refuses. Everything there reads a snapshot and shapes rows. The seam is worth
keeping because these two tables are the part a person has to argue with: a
provider moves between them by somebody deciding it should, not by code
changing.

The two tables are exhaustive over the source on purpose. A provider named by
neither is reported by `--fetch` rather than skipped, so a vendor the source
adds later is a line somebody reads instead of an absence nobody notices.
"""

from __future__ import annotations


#: The wire families this product has an adapter for, by the name the snapshot
#: uses. A family absent from this table is not a gap in the table: it is a
#: family with no adapter, and importing a model onto one would be a row whose
#: every request is composed by nothing.
FAMILIES = {
    "openai-completions": "OPEN_AI_COMPLETIONS",
    "openai-responses": "OPEN_AI_RESPONSES",
    "anthropic-messages": "ANTHROPIC_MESSAGES",
    "google-generative-ai": "GOOGLE_GENERATIVE_LANGUAGE",
    "openai-codex-responses": "OPEN_AI_CODEX_RESPONSES",
}

#: A base URL carrying any of these is an address nobody can reach without
#: substituting something this catalog has no column for.
PLACEHOLDERS = ("{", "}")

#: One imported provider. `origin` is what the row records and what the browser
#: composes beneath; `prefix`/`version` restate the entry that provider needs in
#: `kProviderPathPrefixes` so that the two are written down together — the
#: comment in profile_model_broker_routes.cc says why a row whose list and whose
#: calls go to different places is a vendor nobody meant to describe.
class Imported:
    def __init__(self, pi_id, provider_id, display_name, origin, wire_api,
                 prefix="", version="", get_key_url=None, docs_url=None,
                 subscription=False, existing=False, note=""):
        self.pi_id = pi_id
        self.provider_id = provider_id
        self.display_name = display_name
        self.origin = origin
        self.wire_api = wire_api
        self.prefix = prefix
        self.version = version
        self.get_key_url = get_key_url
        self.docs_url = docs_url
        self.subscription = subscription
        self.existing = existing
        self.note = note


#: Every provider this tool will import, and no others. A provider is here only
#: once its address has been reached: each origin below answered an
#: unauthenticated POST to the address its family composes with the vendor's own
#: error rather than a 404, checked on 2026-09-03.
#:
#: `existing` marks a provider this catalog already declares. For those the tool
#: adds models and never rewrites the provider row, and it checks that the
#: snapshot's own base URL still starts with the origin already recorded — a
#: vendor that has moved its API is a row to revisit by hand, not to top up.
PROVIDERS = [
    # Providers this catalog already carries. Models only.
    Imported("anthropic", "anthropic", "Anthropic", "https://api.anthropic.com",
             "ANTHROPIC_MESSAGES", existing=True),
    Imported("baseten", "baseten", "Baseten", "https://inference.baseten.co",
             "OPEN_AI_COMPLETIONS", existing=True),
    Imported("cerebras", "cerebras", "Cerebras", "https://api.cerebras.ai",
             "OPEN_AI_COMPLETIONS", existing=True),
    Imported("deepseek", "deepseek", "DeepSeek", "https://api.deepseek.com",
             "OPEN_AI_COMPLETIONS", existing=True),
    Imported("fireworks", "fireworks", "Fireworks AI", "https://api.fireworks.ai",
             "OPEN_AI_COMPLETIONS", prefix="/inference", existing=True),
    Imported("google", "google-ai-studio", "Google AI Studio",
             "https://generativelanguage.googleapis.com",
             "GOOGLE_GENERATIVE_LANGUAGE", existing=True),
    Imported("groq", "groq", "Groq", "https://api.groq.com",
             "OPEN_AI_COMPLETIONS", prefix="/openai", existing=True),
    Imported("kimi-coding", "kimi-coding", "Kimi For Coding", "https://api.kimi.com",
             "ANTHROPIC_MESSAGES", prefix="/coding", existing=True),
    Imported("moonshotai", "moonshot", "Moonshot AI", "https://api.moonshot.ai",
             "OPEN_AI_COMPLETIONS", existing=True),
    Imported("openai", "openai", "OpenAI", "https://api.openai.com",
             "OPEN_AI_RESPONSES", existing=True),
    Imported("together", "together", "Together AI", "https://api.together.ai",
             "OPEN_AI_COMPLETIONS", existing=True),
    Imported("xai", "xai", "SpaceXAI", "https://api.x.ai",
             "OPEN_AI_COMPLETIONS", existing=True),

    # Providers this import adds.
    #
    # Several are sold as a prepaid bundle and the reference product files them
    # under subscriptions. None is marked `subscription` here, and the
    # difference is what the flag means in this catalog rather than a
    # disagreement about the product: a subscription row is one reached with a
    # plan-backed credential a person signs in for, and its prices are imputed
    # because nobody is billed per token. Each of these is reached with a key a
    # person pastes, and the source publishes a real per-token rate for every
    # one of their models. Marking them would replace those rates with an
    # estimate and tell the spend cap to enforce it.
    # No presentation block: this vendor publishes no console or reference page
    # this tool could reach, and pointing a person at the API host would send
    # them to a JSON error. A vendor the catalog knows nothing extra about
    # carries no object at all rather than one full of plausible-looking
    # addresses (decision 0094 section 3).
    Imported("ant-ling", "ant-ling", "Ant Ling", "https://api.ant-ling.com",
             "OPEN_AI_COMPLETIONS"),
    Imported("huggingface", "huggingface", "Hugging Face",
             "https://router.huggingface.co", "OPEN_AI_COMPLETIONS",
             get_key_url="https://huggingface.co/settings/tokens",
             docs_url="https://huggingface.co/docs/inference-providers"),
    Imported("moonshotai-cn", "moonshot-cn", "Moonshot AI (China)",
             "https://api.moonshot.cn", "OPEN_AI_COMPLETIONS",
             get_key_url="https://platform.moonshot.cn/console/api-keys",
             docs_url="https://platform.moonshot.cn/docs"),
    Imported("nvidia", "nvidia", "NVIDIA", "https://integrate.api.nvidia.com",
             "OPEN_AI_COMPLETIONS",
             get_key_url="https://build.nvidia.com/settings/api-keys",
             docs_url="https://docs.api.nvidia.com/nim/reference/llm-apis"),
    Imported("xiaomi", "xiaomi", "Xiaomi MiMo", "https://api.xiaomimimo.com",
             "OPEN_AI_COMPLETIONS"),
    Imported("xiaomi-token-plan-ams", "xiaomi-token-plan-ams",
             "Xiaomi MiMo Token Plan (Amsterdam)",
             "https://token-plan-ams.xiaomimimo.com", "OPEN_AI_COMPLETIONS"),
    Imported("xiaomi-token-plan-cn", "xiaomi-token-plan-cn",
             "Xiaomi MiMo Token Plan (China)",
             "https://token-plan-cn.xiaomimimo.com", "OPEN_AI_COMPLETIONS"),
    Imported("xiaomi-token-plan-sgp", "xiaomi-token-plan-sgp",
             "Xiaomi MiMo Token Plan (Singapore)",
             "https://token-plan-sgp.xiaomimimo.com", "OPEN_AI_COMPLETIONS"),
    Imported("qwen-token-plan", "qwen-token-plan", "Qwen Token Plan",
             "https://token-plan.ap-southeast-1.maas.aliyuncs.com",
             "OPEN_AI_COMPLETIONS", prefix="/compatible-mode",
             get_key_url="https://www.alibabacloud.com/help/en/model-studio/get-api-key",
             docs_url="https://www.alibabacloud.com/help/en/model-studio/token-plan-personal-overview"),
    Imported("qwen-token-plan-cn", "qwen-token-plan-cn", "Qwen Token Plan (China)",
             "https://token-plan.cn-beijing.maas.aliyuncs.com",
             "OPEN_AI_COMPLETIONS", prefix="/compatible-mode"),
    Imported("zai-coding-cn", "zai-coding-cn", "Z.AI GLM Coding Plan (China)",
             "https://open.bigmodel.cn", "OPEN_AI_COMPLETIONS",
             prefix="/api/coding/paas", version="/v4",
             docs_url="https://open.bigmodel.cn/dev/api"),
    Imported("minimax-cn", "minimax-cn", "MiniMax (China)",
             "https://api.minimaxi.com", "ANTHROPIC_MESSAGES", prefix="/anthropic",
             docs_url="https://platform.minimaxi.com/docs"),
    Imported("opencode", "opencode", "OpenCode Zen", "https://opencode.ai",
             "OPEN_AI_COMPLETIONS", prefix="/zen",
             get_key_url="https://opencode.ai/auth",
             docs_url="https://opencode.ai/docs/zen"),
    Imported("opencode-go", "opencode-go", "OpenCode Go", "https://opencode.ai",
             "OPEN_AI_COMPLETIONS", prefix="/zen/go",
             get_key_url="https://opencode.ai/auth",
             docs_url="https://opencode.ai/docs/zen"),
    Imported("vercel-ai-gateway", "vercel-ai-gateway", "Vercel AI Gateway",
             "https://ai-gateway.vercel.sh", "ANTHROPIC_MESSAGES",
             get_key_url="https://vercel.com/dashboard/ai-gateway/api-keys",
             docs_url="https://vercel.com/docs/ai-gateway"),
]

#: Providers the snapshot carries and this tool deliberately does not import,
#: each with the reason. Written down because an unexplained absence reads as an
#: oversight, and the next person to run this would otherwise re-derive all of
#: it. `check --strict` fails if the snapshot grows a provider named by neither
#: table, so this register cannot silently go stale.
REFUSED = {
    "amazon-bedrock": "served on bedrock-converse-stream, a wire family this "
                      "product has no adapter for, and reached with AWS "
                      "SigV4 request signing, which is authentication "
                      "machinery the browser does not have",
    "azure-openai-responses": "every base URL names a deployment this catalog "
                              "has no column for, and the family is not one "
                              "of the five",
    "google-vertex": "served on the google-vertex family, which this product "
                     "has no adapter for, and its base URL substitutes a "
                     "region",
    "cloudflare-ai-gateway": "its base URL substitutes an account id and a "
                             "gateway id, so no fixed origin reaches it",
    "cloudflare-workers-ai": "its base URL substitutes an account id, so no "
                             "fixed origin reaches it. This catalog used to "
                             "reach the vendor by a hand-written row that "
                             "composed the account elsewhere; decision 0214 "
                             "withdrew that row, so there is nothing for an "
                             "import to top up either",
    "mistral": "the snapshot serves this vendor on mistral-conversations, a "
               "family this product has no adapter for. The vendor's "
               "OpenAI-shaped API is reached by this catalog's own hand-written "
               "mistral row, whose numbers are quoted from the vendor's model "
               "pages",
    "openai-codex": "a subscription reached through the openai row's OAUTH "
                    "method rather than a second descriptor for one vendor "
                    "(decision 0029 section 2)",
    "openrouter": "carried as DYNAMIC_LISTING: this vendor publishes a model "
                  "list the core reads at run time, which is fresher than any "
                  "snapshot and is already wired",
    "github-copilot": "the snapshot names api.individual.githubcopilot.com "
                      "and this catalog's row records githubcopilot.com. Two "
                      "hosts is a row to settle by hand, not to top up",
    "minimax": "the snapshot serves this vendor's models on anthropic-messages "
               "beneath /anthropic, and this catalog's row reaches it as "
               "OpenAI-shaped at the bare origin. Two different APIs of one "
               "vendor is a row to settle by hand",
    "zai": "the snapshot's base URL is this vendor's coding-plan path, and "
           "this catalog's zai row is the metered API at /api/paas/v4. The "
           "coding plan is imported as its own China row and the international "
           "one is left for a row somebody checks",
    "qwen-token-plan-individual": "the same host and the same compatible-mode "
                                  "path as qwen-token-plan, differing only by "
                                  "which plan was bought, which is not a "
                                  "second endpoint",
    "deepinfra": "carried by this catalog's own hand-written row, whose numbers "
                 "are quoted from this vendor's own public model listing",
}
