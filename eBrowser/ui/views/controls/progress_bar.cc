// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/progress_bar.h"

#include <algorithm>
#include <string>

#include "base/logging.h"
#include "base/macros.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkXfermode.h"
#include "third_party/skia/include/effects/SkGradientShader.h"
#include "ui/accessibility/ax_view_state.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/native_theme/native_theme.h"

namespace views {

namespace {

// In DP, the amount to round the corners of the progress bar (both bg and
// fg, aka slice).
const int kCornerRadius = 3;

// Adds a rectangle to the path. The corners will be rounded if there is room.
void AddPossiblyRoundRectToPath(const gfx::Rect& rectangle, SkPath* path) {
  if (rectangle.height() < kCornerRadius) {
    path->addRect(gfx::RectToSkRect(rectangle));
  } else {
    path->addRoundRect(gfx::RectToSkRect(rectangle), kCornerRadius,
                       kCornerRadius);
  }
}

}  // namespace

// static
const char ProgressBar::kViewClassName[] = "ProgressBar";

ProgressBar::ProgressBar(int preferred_height)
    : preferred_height_(preferred_height) {}

ProgressBar::~ProgressBar() {
}

void ProgressBar::GetAccessibleState(ui::AXViewState* state) {
  state->role = ui::AX_ROLE_PROGRESS_INDICATOR;
  state->AddStateFlag(ui::AX_STATE_READ_ONLY);
}

gfx::Size ProgressBar::GetPreferredSize() const {
  // The width will typically be ignored.
  gfx::Size pref_size(1, preferred_height_);
  gfx::Insets insets = GetInsets();
  pref_size.Enlarge(insets.width(), insets.height());
  return pref_size;
}

const char* ProgressBar::GetClassName() const {
  return kViewClassName;
}

void ProgressBar::OnPaint(gfx::Canvas* canvas) {
  if (IsIndeterminate())
    return OnPaintIndeterminate(canvas);

  gfx::Rect content_bounds = GetContentsBounds();

  // Draw background.
  SkPath background_path;
  AddPossiblyRoundRectToPath(content_bounds, &background_path);
  SkPaint background_paint;
  background_paint.setStyle(SkPaint::kFill_Style);
  background_paint.setFlags(SkPaint::kAntiAlias_Flag);
  background_paint.setColor(GetBackgroundColor());
  canvas->DrawPath(background_path, background_paint);

  // Draw slice.
  SkPath slice_path;
  const int slice_width = static_cast<int>(
      content_bounds.width() * std::min(current_value_, 1.0) + 0.5);
  if (slice_width < 1)
    return;

  gfx::Rect slice_bounds = content_bounds;
  slice_bounds.set_width(slice_width);
  AddPossiblyRoundRectToPath(slice_bounds, &slice_path);

  SkPaint slice_paint;
  slice_paint.setStyle(SkPaint::kFill_Style);
  slice_paint.setFlags(SkPaint::kAntiAlias_Flag);
  slice_paint.setColor(GetForegroundColor());
  canvas->DrawPath(slice_path, slice_paint);
}

void ProgressBar::SetValue(double value) {
  double adjusted_value = (value < 0.0 || value > 1.0) ? -1.0 : value;

  if (adjusted_value == current_value_)
    return;

  current_value_ = adjusted_value;
  if (IsIndeterminate()) {
    indeterminate_bar_animation_.reset(new gfx::LinearAnimation(this));
    indeterminate_bar_animation_->SetDuration(2000);  // In milliseconds.
    indeterminate_bar_animation_->Start();
  } else {
    indeterminate_bar_animation_.reset();
    SchedulePaint();
  }
}

SkColor ProgressBar::GetForegroundColor() const {
  return GetNativeTheme()->GetSystemColor(
      ui::NativeTheme::kColorId_ProminentButtonColor);
}

SkColor ProgressBar::GetBackgroundColor() const {
  // The default foreground is GoogleBlue500, and the default background is
  // that color but 80% lighter.
  return color_utils::BlendTowardOppositeLuma(GetForegroundColor(), 0xCC);
}

void ProgressBar::AnimationProgressed(const gfx::Animation* animation) {
  DCHECK_EQ(animation, indeterminate_bar_animation_.get());
  DCHECK(IsIndeterminate());
  SchedulePaint();
}

void ProgressBar::AnimationEnded(const gfx::Animation* animation) {
  DCHECK_EQ(animation, indeterminate_bar_animation_.get());
  // Restarts animation.
  if (IsIndeterminate())
    indeterminate_bar_animation_->Start();
}

bool ProgressBar::IsIndeterminate() {
  return current_value_ < 0.0;
}

void ProgressBar::OnPaintIndeterminate(gfx::Canvas* canvas) {
  gfx::Rect content_bounds = GetContentsBounds();

  // Draw background.
  SkPath background_path;
  AddPossiblyRoundRectToPath(content_bounds, &background_path);
  SkPaint background_paint;
  background_paint.setStyle(SkPaint::kFill_Style);
  background_paint.setFlags(SkPaint::kAntiAlias_Flag);
  background_paint.setColor(GetBackgroundColor());
  canvas->DrawPath(background_path, background_paint);

  // Draw slice.
  SkPath slice_path;
  double time = indeterminate_bar_animation_->GetCurrentValue();

  // The animation spec corresponds to the material design lite's parameter.
  // (cf. https://github.com/google/material-design-lite/)
  double bar1_left;
  double bar1_width;
  double bar2_left;
  double bar2_width;
  if (time < 0.50) {
    bar1_left = time / 2;
    bar1_width = time * 1.5;
    bar2_left = 0;
    bar2_width = 0;
  } else if (time < 0.75) {
    bar1_left = time * 3 - 1.25;
    bar1_width = 0.75 - (time - 0.5) * 3;
    bar2_left = 0;
    bar2_width = time - 0.5;
  } else {
    bar1_left = 1;
    bar1_width = 0;
    bar2_left = (time - 0.75) * 4;
    bar2_width = 0.25 - (time - 0.75);
  }

  int bar1_x = static_cast<int>(content_bounds.width() * bar1_left);
  int bar1_w =
      std::min(static_cast<int>(content_bounds.width() * bar1_width + 0.5),
               content_bounds.width() - bar1_x);
  int bar2_x = static_cast<int>(content_bounds.width() * bar2_left);
  int bar2_w =
      std::min(static_cast<int>(content_bounds.width() * bar2_width + 0.5),
               content_bounds.width() - bar2_x);

  gfx::Rect slice_bounds = content_bounds;
  slice_bounds.set_x(content_bounds.x() + bar1_x);
  slice_bounds.set_width(bar1_w);
  AddPossiblyRoundRectToPath(slice_bounds, &slice_path);
  slice_bounds.set_x(content_bounds.x() + bar2_x);
  slice_bounds.set_width(bar2_w);
  AddPossiblyRoundRectToPath(slice_bounds, &slice_path);

  SkPaint slice_paint;
  slice_paint.setStyle(SkPaint::kFill_Style);
  slice_paint.setFlags(SkPaint::kAntiAlias_Flag);
  slice_paint.setColor(GetForegroundColor());
  canvas->DrawPath(slice_path, slice_paint);
}

}  // namespace views
