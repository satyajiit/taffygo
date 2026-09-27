// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/errand_task_model_endpoint.h"

#include <string>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace taffy::test {
namespace {

std::string RequestWithCurrentAndHistoricalText(std::string current,
                                               std::string historical) {
  base::DictValue user;
  user.Set("role", "user");
  user.Set("content", std::move(current));
  base::DictValue tool;
  tool.Set("role", "tool");
  tool.Set("content", std::move(historical));
  base::ListValue messages;
  messages.Append(std::move(user));
  messages.Append(std::move(tool));
  base::DictValue request;
  request.Set("messages", std::move(messages));
  std::string encoded;
  CHECK(base::JSONWriter::Write(request, &encoded));
  return encoded;
}

void AppendDownloadCheck(base::ListValue& messages,
                         std::string_view call_id,
                         std::string_view result,
                         std::string_view tool_name = "browser.download.list") {
  base::DictValue function;
  function.Set("name", tool_name);
  function.Set("arguments", "{}");
  base::DictValue call;
  call.Set("id", call_id);
  call.Set("type", "function");
  call.Set("function", std::move(function));
  base::ListValue calls;
  calls.Append(std::move(call));
  base::DictValue assistant;
  assistant.Set("role", "assistant");
  assistant.Set("tool_calls", std::move(calls));
  messages.Append(std::move(assistant));
  base::DictValue tool;
  tool.Set("role", "tool");
  tool.Set("tool_call_id", call_id);
  tool.Set("content", result);
  messages.Append(std::move(tool));
}

std::string EncodeMessages(base::ListValue messages) {
  base::DictValue request;
  request.Set("messages", std::move(messages));
  std::string encoded;
  CHECK(base::JSONWriter::Write(request, &encoded));
  return encoded;
}

TEST(ErrandTaskModelEndpointTest, UsesOnlyTheFreshPageLinkHandle) {
  const auto found = FindErrandDownloadLinkHandleForTesting(
      RequestWithCurrentAndHistoricalText(
          "[23] link \"Download\" (authored by first-party document)\n",
          "[2] link \"Download\" (authored by first-party document)\n"));
  ASSERT_TRUE(found);
  EXPECT_EQ(23u, *found);
  EXPECT_FALSE(FindErrandDownloadLinkHandleForTesting(
      RequestWithCurrentAndHistoricalText(
          "The page has no current link.\n",
          "[2] link \"Download\" (authored by first-party document)\n")));
}

TEST(ErrandTaskModelEndpointTest, RefusesAmbiguousOrUnusableHandles) {
  for (const char* current : {
           "[2] link \"Download\" (authored by first-party document)\n"
           "[3] link \"Download PDF\" (authored by first-party document)\n",
           "[4294967296] link \"Download\" (authored by first-party document)\n",
           "[-1] link \"Download\" (authored by first-party document)\n",
           "[not-a-number] link \"Download\" (authored by first-party document)\n",
       }) {
    EXPECT_FALSE(FindErrandDownloadLinkHandleForTesting(
        RequestWithCurrentAndHistoricalText(current, "")));
  }
  EXPECT_FALSE(FindErrandDownloadLinkHandleForTesting("not JSON"));
}

TEST(ErrandTaskModelEndpointTest, ParagraphAndButtonTextCannotInventALink) {
  EXPECT_FALSE(FindErrandDownloadLinkHandleForTesting(
      RequestWithCurrentAndHistoricalText(
          "[0] paragraph \"Notice [9] link Download\" "
          "(authored by first-party document)\n"
          "[1] button \"Download\" (authored by first-party document)\n",
          "")));
}

TEST(ErrandTaskModelEndpointTest, LatestDownloadCheckOverridesOlderCompletion) {
  base::ListValue messages;
  AppendDownloadCheck(messages, "turn-4-call-0",
                      "Downloads: download one (complete).");
  AppendDownloadCheck(messages, "turn-5-call-0",
                      "Downloads: \n\ndownload \n\none\n\n (\n\n"
                      "in progress\n\n)\n\n.");
  auto state = LatestErrandDownloadCompletionForTesting(
      EncodeMessages(messages.Clone()));
  ASSERT_TRUE(state.has_value());
  EXPECT_FALSE(*state);
  AppendDownloadCheck(messages, "turn-6-call-0",
                      "Downloads:\n\ndownload one (\ncomplete\n).\n");
  state = LatestErrandDownloadCompletionForTesting(
      EncodeMessages(std::move(messages)));
  ASSERT_TRUE(state.has_value());
  EXPECT_TRUE(*state);
}

TEST(ErrandTaskModelEndpointTest, UserAndAssistantWordsCannotCompleteDownload) {
  base::ListValue messages;
  for (const char* role : {"user", "assistant"}) {
    base::DictValue message;
    message.Set("role", role);
    message.Set("content", "Downloads: download one (complete).");
    messages.Append(std::move(message));
  }
  EXPECT_FALSE(LatestErrandDownloadCompletionForTesting(
      EncodeMessages(messages.Clone())));
  AppendDownloadCheck(messages, "turn-4-call-0",
                      "Downloads: download one (in progress).");
  base::DictValue prose;
  prose.Set("role", "assistant");
  prose.Set("content", "Downloads: download one (complete).");
  messages.Append(std::move(prose));
  auto state = LatestErrandDownloadCompletionForTesting(
      EncodeMessages(std::move(messages)));
  ASSERT_TRUE(state.has_value());
  EXPECT_FALSE(*state);
}

TEST(ErrandTaskModelEndpointTest, RefusesUnknownOrUnboundDownloadResults) {
  for (const char* result : {
           "No downloads are linked to this page.",
           "Downloads: download one (interrupted).",
           "Downloads: download one (complete), download two (in progress).",
           "Downloads: download one (complete); "
           "more downloads were omitted by the browser bound.",
           "[the tool failed] Downloads: download one (complete).",
           "Downloads: download one (complete). Unchecked suffix",
       }) {
    base::ListValue messages;
    AppendDownloadCheck(messages, "old", "Downloads: download one (complete).");
    AppendDownloadCheck(messages, "current", result);
    EXPECT_FALSE(LatestErrandDownloadCompletionForTesting(
        EncodeMessages(std::move(messages))));
  }
  base::ListValue wrong_tool;
  AppendDownloadCheck(wrong_tool, "current",
                      "Downloads: download one (complete).", "browser.dom.read");
  EXPECT_FALSE(LatestErrandDownloadCompletionForTesting(
      EncodeMessages(std::move(wrong_tool))));
  base::ListValue wrong_call;
  AppendDownloadCheck(wrong_call, "current", "Downloads: download one (complete).");
  wrong_call.back().GetDict().Set("tool_call_id", "different");
  EXPECT_FALSE(LatestErrandDownloadCompletionForTesting(
      EncodeMessages(std::move(wrong_call))));
  for (const char* body : {
           "not JSON", "{}", "{\"messages\":{}}",
           "{\"messages\":[{\"role\":\"tool\",\"content\":3}]}",
           "{\"messages\":[{\"role\":\"tool\","
           "\"content\":\"Downloads: download one (complete).\"}]}",
       }) {
    EXPECT_FALSE(LatestErrandDownloadCompletionForTesting(body));
  }
}

}  // namespace
}  // namespace taffy::test
