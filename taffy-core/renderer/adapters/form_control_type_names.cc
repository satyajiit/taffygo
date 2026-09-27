// Copyright (c) 2026 Matterward Labs Private Limited.
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "taffy/renderer/adapters/form_control_type_names.h"

namespace taffy::form_control_type_names {

std::string ControlTypeString(blink::mojom::FormControlType type) {
  switch (type) {
    case blink::mojom::FormControlType::kButtonButton:
    case blink::mojom::FormControlType::kInputButton:
      return "button";
    case blink::mojom::FormControlType::kButtonSubmit:
    case blink::mojom::FormControlType::kInputSubmit:
      return "submit";
    case blink::mojom::FormControlType::kButtonReset:
    case blink::mojom::FormControlType::kInputReset:
      return "reset";
    case blink::mojom::FormControlType::kButtonPopover:
      return "popover";
    case blink::mojom::FormControlType::kFieldset:
      return "fieldset";
    case blink::mojom::FormControlType::kInputCheckbox:
      return "checkbox";
    case blink::mojom::FormControlType::kInputColor:
      return "color";
    case blink::mojom::FormControlType::kInputDate:
      return "date";
    case blink::mojom::FormControlType::kInputDatetimeLocal:
      return "datetime-local";
    case blink::mojom::FormControlType::kInputEmail:
      return "email";
    case blink::mojom::FormControlType::kInputFile:
      return "file";
    case blink::mojom::FormControlType::kInputHidden:
      return "hidden";
    case blink::mojom::FormControlType::kInputImage:
      return "image";
    case blink::mojom::FormControlType::kInputMonth:
      return "month";
    case blink::mojom::FormControlType::kInputNumber:
      return "number";
    case blink::mojom::FormControlType::kInputPassword:
      return "password";
    case blink::mojom::FormControlType::kInputRadio:
      return "radio";
    case blink::mojom::FormControlType::kInputRange:
      return "range";
    case blink::mojom::FormControlType::kInputSearch:
      return "search";
    case blink::mojom::FormControlType::kInputTelephone:
      return "tel";
    case blink::mojom::FormControlType::kInputText:
      return "text";
    case blink::mojom::FormControlType::kInputTime:
      return "time";
    case blink::mojom::FormControlType::kInputUrl:
      return "url";
    case blink::mojom::FormControlType::kInputWeek:
      return "week";
    case blink::mojom::FormControlType::kOutput:
      return "output";
    case blink::mojom::FormControlType::kSelectOne:
      return "select-one";
    case blink::mojom::FormControlType::kSelectMultiple:
      return "select-multiple";
    case blink::mojom::FormControlType::kTextArea:
      return "textarea";
  }
}

bool IsSetTextControl(std::string_view type) {
  return type == "text" || type == "search" || type == "email" ||
         type == "tel" || type == "url" || type == "number" ||
         type == "date" || type == "datetime-local" || type == "month" ||
         type == "time" || type == "week" || type == "color" ||
         type == "range" || type == "textarea";
}
}  // namespace taffy::form_control_type_names
