// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/structured_data_claims.h"

#include <inttypes.h>

#include <limits>
#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "taffy/renderer/adapters/adapter.h"
#include "taffy/renderer/adapters/bounded_web_text.h"
#include "taffy/renderer/adapters/structured_data_vocabulary.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_element_collection.h"
#include "third_party/blink/public/web/web_node.h"

// VERIFY AT SP-04:
//   * blink::WebDocument::GetElementsByHTMLTagName() and the
//     blink::WebElementCollection iteration idiom (FirstItem/NextItem).
//   * base::JSONReader::Read() option names and whether the maximum-depth
//     argument is still the third parameter at the pin. The depth bound
//     matters: JSON-LD is attacker-controlled and deeply nested input is the
//     cheapest way to burn renderer stack.
//   * That reading TextContent() of a <script> element does not run it. It
//     does not, but a reviewer will ask, and the fixture that proves it
//     belongs in the corpus.
//   * blink::WebElement::GetAttribute() and blink::WebNode::To<T>() spellings
//     at the pin, and whether GetAttribute() on an element detached mid-walk
//     is safe.

namespace taffy::structured_data_claims {

using structured_data_vocabulary::ComparisonForm;
using structured_data_vocabulary::IsAllowedProperty;
using structured_data_vocabulary::Utf8;

namespace {

bool CollectJsonLd(ExtractionContext& context,
                   const base::Value& value,
                   const std::string& entity_type,
                   const std::string& locator,
                   CollectedClaims& out) {
  if (!context.ledger->CheckDeadline() ||
      out.claims.size() >= context.limits->snapshot().max_nodes()) {
    if (!context.ledger->exhausted()) {
      out.claim_limit_reached = true;
      context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
    }
    return false;
  }
  if (value.is_dict()) {
    // Confirmed at the 152 pin: the nested base::Value::Dict and
    // base::Value::List types were lifted out to base::DictValue and
    // base::ListValue, with no compatibility alias left behind. Same class,
    // same API - Value::GetDict() returns a const DictValue&.
    const base::DictValue& dict = value.GetDict();
    std::string type = entity_type;
    if (const std::string* declared = dict.FindString("@type")) {
      type = *declared;
    }
    for (const auto [key, child] : dict) {
      if (!context.ledger->CheckDeadline() ||
          out.claims.size() >= context.limits->snapshot().max_nodes()) {
        if (!context.ledger->exhausted()) {
          out.claim_limit_reached = true;
          context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
        }
        return false;
      }
      if (key.starts_with("@")) {
        continue;
      }
      if (child.is_dict() || child.is_list()) {
        if (!CollectJsonLd(context, child, type, locator + "/" + key, out)) {
          return false;
        }
        continue;
      }
      if (!IsAllowedProperty(key)) {
        continue;
      }
      std::string text;
      if (child.is_string()) {
        text = child.GetString();
      } else if (child.is_int()) {
        text = base::NumberToString(child.GetInt());
      } else if (child.is_double()) {
        text = base::NumberToString(child.GetDouble());
      } else if (child.is_bool()) {
        text = child.GetBool() ? "true" : "false";
      } else {
        continue;
      }
      StructuredClaim claim;
      claim.property = key;
      claim.comparison_form = ComparisonForm(text);
      claim.value = std::move(text);
      claim.source = SourceKind::kJsonLd;
      claim.locator = locator + "/" + key;
      claim.entity_type = type;
      claim.authored_for_machines = true;
      out.claims.push_back(std::move(claim));
    }
    return true;
  }
  if (value.is_list()) {
    int index = 0;
    for (const base::Value& child : value.GetList()) {
      if (!CollectJsonLd(context, child, entity_type,
                         base::StringPrintf("%s/%d", locator.c_str(), index++),
                         out)) {
        return false;
      }
    }
  }
  return true;
}

void CollectJsonLdBlocks(ExtractionContext& context,
                         const blink::WebDocument& document,
                         CollectedClaims& out) {
  const StructuredDataLimits& limits = context.limits->structured_data();
  uint32_t blocks_read = 0;
  blink::WebElementCollection scripts =
      document.GetElementsByHTMLTagName(blink::WebString::FromUtf8("script"));
  for (blink::WebElement script = scripts.FirstItem(); !script.IsNull();
       script = scripts.NextItem()) {
    if (context.ledger->exhausted() || !context.ledger->CheckDeadline()) {
      break;
    }
    const std::string type = base::ToLowerASCII(
        Utf8(script.GetAttribute(blink::WebString::FromUtf8("type"))));
    if (type != "application/ld+json") {
      continue;
    }
    if (blocks_read >= limits.max_blocks()) {
      out.block_limit_reached = true;
      break;
    }
    ++blocks_read;

    const unsigned int max_block_units =
        limits.max_block_bytes() < std::numeric_limits<unsigned int>::max()
            ? limits.max_block_bytes() + 1u
            : limits.max_block_bytes();
    const blink::WebString raw_text =
        script.TextContentAbridged(max_block_units);
    if (raw_text.length() > limits.max_block_bytes()) {
      // UTF-8 is never shorter than the UTF-16 code-unit count. Reject this
      // before conversion so an oversized block cannot set peak memory.
      out.oversized_block_skipped = true;
      continue;
    }
    const std::string raw = Utf8(raw_text);
    if (raw.size() > limits.max_block_bytes()) {
      // Bounded before parsing, not after. A page does not get to choose the
      // renderer's peak memory.
      out.oversized_block_skipped = true;
      continue;
    }
    if (!context.ledger->ChargeBytes(raw.size())) {
      break;
    }

    // Strict parsing: a page's metadata does not get Chromium's parser
    // extensions.
    std::optional<base::Value> parsed = base::JSONReader::Read(
        raw, base::JSON_PARSE_RFC, limits.max_json_depth());
    if (!parsed.has_value()) {
      // Malformed metadata is skipped and named. It is never repaired by
      // guessing, and it never fails the whole snapshot.
      out.malformed_block_skipped = true;
      continue;
    }
    if (!CollectJsonLd(context, parsed.value(), std::string(),
                       base::StringPrintf("json-ld/%u", blocks_read), out)) {
      break;
    }
  }
}

// Walked from the body with the same explicit stack the DOM adapter uses,
// reading only the three microdata attributes. Recursion over a page's DOM is
// a stack-depth attack a hostile fixture will absolutely try.
void CollectMicrodata(ExtractionContext& context,
                      const blink::WebDocument& document,
                      CollectedClaims& out) {
  const blink::WebElement body = document.Body();
  if (body.IsNull()) {
    return;
  }
  struct Entry {
    blink::WebNode node;
    uint32_t depth;
    std::string item_type;
  };
  std::vector<Entry> stack;
  stack.push_back({body, 0u, std::string()});
  while (!stack.empty()) {
    const Entry current = stack.back();
    stack.pop_back();
    if (context.ledger->exhausted() || !context.ledger->CheckDeadline()) {
      break;
    }
    if (!context.ledger->WithinDepth(current.depth)) {
      continue;
    }
    if (!current.node.IsElementNode()) {
      continue;
    }
    const blink::WebElement element = current.node.To<blink::WebElement>();

    // The item type is scoped to the subtree that declared it, which is what
    // makes two entities on one page distinguishable. Carrying it in the stack
    // entry rather than in a running variable is the difference between
    // correct scoping and whichever type was seen last.
    std::string item_type = current.item_type;
    const std::string declared =
        Utf8(element.GetAttribute(blink::WebString::FromUtf8("itemtype")));
    if (!declared.empty()) {
      item_type = declared;
    }

    const std::string prop =
        Utf8(element.GetAttribute(blink::WebString::FromUtf8("itemprop")));
    if (!prop.empty() && IsAllowedProperty(prop)) {
      if (out.claims.size() >= context.limits->snapshot().max_nodes()) {
        out.claim_limit_reached = true;
        context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
        break;
      }
      // `content` when present is the authored machine value; otherwise the
      // element's own rendered text. The difference is recorded, because a
      // machine-only value is exactly the kind that goes stale unnoticed.
      const blink::WebString machine_value =
          element.GetAttribute(blink::WebString::FromUtf8("content"));
      const bool machine_only = !machine_value.IsEmpty();
      BoundedWebText bounded =
          machine_only
              ? BoundWebText(machine_value, *context.ledger,
                             context.limits->snapshot().max_text_bytes())
              : BoundElementTextContent(
                    element, *context.ledger,
                    context.limits->snapshot().max_text_bytes());
      std::string value = std::move(bounded.text);
      if (!value.empty()) {
        StructuredClaim claim;
        claim.property = prop;
        claim.comparison_form = ComparisonForm(value);
        claim.value = std::move(value);
        claim.source = SourceKind::kMicrodata;
        claim.locator =
            base::StringPrintf("microdata/%" PRId64, element.GetDomNodeId());
        claim.entity_type = item_type;
        claim.authored_for_machines = machine_only;
        out.claims.push_back(std::move(claim));
      }
    }

    for (blink::WebNode child = element.FirstChild(); !child.IsNull();
         child = child.NextSibling()) {
      if (!context.ledger->CheckDeadline()) {
        break;
      }
      if (!child.IsElementNode()) {
        continue;
      }
      if (stack.size() >= context.limits->snapshot().max_nodes()) {
        out.traversal_limit_reached = true;
        context.ledger->NoteOmittedNode(/*could_change_answer=*/true);
        break;
      }
      stack.push_back({child, current.depth + 1, item_type});
    }
  }
}

}  // namespace

StructuredClaim::StructuredClaim() = default;
StructuredClaim::StructuredClaim(const StructuredClaim&) = default;
StructuredClaim::StructuredClaim(StructuredClaim&&) = default;
StructuredClaim& StructuredClaim::operator=(const StructuredClaim&) = default;
StructuredClaim& StructuredClaim::operator=(StructuredClaim&&) = default;
StructuredClaim::~StructuredClaim() = default;

CollectedClaims::CollectedClaims() = default;
CollectedClaims::CollectedClaims(CollectedClaims&&) = default;
CollectedClaims& CollectedClaims::operator=(CollectedClaims&&) = default;
CollectedClaims::~CollectedClaims() = default;

std::string EntityScopedProperty(const StructuredClaim& claim) {
  return base::StrCat({claim.entity_type, "\x1f", claim.property});
}

CollectedClaims Collect(ExtractionContext& context,
                        const blink::WebDocument& document) {
  CollectedClaims collected;
  CollectJsonLdBlocks(context, document, collected);
  CollectMicrodata(context, document, collected);
  return collected;
}

}  // namespace taffy::structured_data_claims
