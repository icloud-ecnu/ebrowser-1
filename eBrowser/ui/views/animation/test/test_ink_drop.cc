// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/animation/test/test_ink_drop.h"

namespace views {
namespace test {

TestInkDrop::TestInkDrop() : state_(InkDropState::HIDDEN), is_hovered_(false) {}
TestInkDrop::~TestInkDrop() {}

InkDropState TestInkDrop::GetTargetInkDropState() const {
  return state_;
}

void TestInkDrop::AnimateToState(InkDropState ink_drop_state) {
  state_ = ink_drop_state;
}

void TestInkDrop::SnapToActivated() {
  state_ = InkDropState::ACTIVATED;
}

void TestInkDrop::SetHovered(bool is_hovered) {
  is_hovered_ = is_hovered;
}

void TestInkDrop::SetFocused(bool is_focused) {}

}  // namespace test
}  // namespace views
