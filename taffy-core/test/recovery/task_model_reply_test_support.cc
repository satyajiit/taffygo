// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/test/recovery/task_model_reply_test_support.h"

#include <utility>

#include "base/check.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"

namespace taffy::test {
namespace {

std::string Encode(base::DictValue value) {
  std::string encoded;
  CHECK(base::JSONWriter::Write(value, &encoded));
  return encoded;
}

std::string StreamedReply(base::DictValue delta, std::string finish_reason) {
  base::DictValue choice;
  choice.Set("delta", std::move(delta));
  choice.Set("finish_reason", std::move(finish_reason));
  base::ListValue choices;
  choices.Append(std::move(choice));
  base::DictValue usage;
  usage.Set("prompt_tokens", 20);
  usage.Set("completion_tokens", 10);
  base::DictValue root;
  root.Set("choices", std::move(choices));
  root.Set("usage", std::move(usage));
  return base::StrCat(
      {"data: ", Encode(std::move(root)), "\n\ndata: [DONE]\n\n"});
}

}  // namespace

std::string StreamedToolReply(std::string id,
                              std::string name,
                              base::DictValue arguments) {
  base::DictValue function;
  function.Set("name", std::move(name));
  function.Set("arguments", Encode(std::move(arguments)));
  base::DictValue call;
  call.Set("index", 0);
  call.Set("id", std::move(id));
  call.Set("type", "function");
  call.Set("function", std::move(function));
  base::ListValue calls;
  calls.Append(std::move(call));
  base::DictValue delta;
  delta.Set("tool_calls", std::move(calls));
  return StreamedReply(std::move(delta), "tool_calls");
}

std::string StreamedFinalReply(std::string text) {
  base::DictValue delta;
  delta.Set("content", std::move(text));
  return StreamedReply(std::move(delta), "stop");
}

}  // namespace taffy::test
