// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_DISPLAY_FAKE_DISPLAY_DELEGATE_H_
#define UI_DISPLAY_FAKE_DISPLAY_DELEGATE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/observer_list.h"
#include "ui/display/display_export.h"
#include "ui/display/fake_display_snapshot.h"
#include "ui/display/types/fake_display_controller.h"
#include "ui/display/types/native_display_delegate.h"

namespace display {

// A NativeDisplayDelegate implementation that manages fake displays. Fake
// displays mimic physical displays but do not actually exist. This is because
// Chrome OS does not take over the entire display when running off device and
// instead runs inside windows provided by the parent OS. Fake displays allow us
// to simulate different connected display states off device and to test display
// configuration and display management code.
//
// The size and number of displays can controlled via --screen-config=X
// command line flag. The format is as follows, where [] are optional:
//   native_mode[#other_modes][^dpi][/options]
//
// native_mode: the native display mode, with format:
//   HxW[%R]
//     H: display height in pixels [int]
//     W: display width in pixels [int]
//     R: display refresh rate [float]
//
// other_modes: list of other of display modes, with format:
//   #HxW[%R][:HxW[%R]]
//     H,W,R: same meaning as in native_mode.
//   Note: The first mode is delimited with '#' and any subsequent modes are
//         delimited with ':'.
//
// dpi: display DPI used to set physical size, with format:
//   ^D
//     D: display DPI [int]
//
// options: options to set on display snapshot, with format:
//   /[a][c][i][o]
//     a: display is aspect preserving [literal a]
//     c: display has color correction matrix [literal c]
//     i: display is internal [literal i]
//     o: display has overscan [literal o]
//
// Examples:
//
// Two 800x800 displays, with first display as internal display:
//  --screen-config=800x800/i,800x800
// One 1920x1080 display as internal display with alternate resolutions:
//  --screen-config=1920x1080#1600x900:1280x720/i
// One 1600x900 display with 120 refresh rate and high-DPI:
//   --screen-config=1600x900%120^300
// No displays:
//  --screen-config=none
//
// FakeDisplayDelegate also implements FakeDisplayController which provides a
// way to change the display state at runtime.
class DISPLAY_EXPORT FakeDisplayDelegate : public ui::NativeDisplayDelegate,
                                           public FakeDisplayController {
 public:
  FakeDisplayDelegate();
  ~FakeDisplayDelegate() override;

  // FakeDisplayController:
  int64_t AddDisplay(const gfx::Size& display_size) override;
  bool AddDisplay(std::unique_ptr<ui::DisplaySnapshot> display) override;
  bool RemoveDisplay(int64_t display_id) override;

  // NativeDisplayDelegate overrides:
  void Initialize() override;
  void GrabServer() override;
  void UngrabServer() override;
  void TakeDisplayControl(const ui::DisplayControlCallback& callback) override;
  void RelinquishDisplayControl(
      const ui::DisplayControlCallback& callback) override;
  void SyncWithServer() override;
  void SetBackgroundColor(uint32_t color_argb) override;
  void ForceDPMSOn() override;
  void GetDisplays(const ui::GetDisplaysCallback& callback) override;
  void AddMode(const ui::DisplaySnapshot& output,
               const ui::DisplayMode* mode) override;
  void Configure(const ui::DisplaySnapshot& output,
                 const ui::DisplayMode* mode,
                 const gfx::Point& origin,
                 const ui::ConfigureCallback& callback) override;
  void CreateFrameBuffer(const gfx::Size& size) override;
  void GetHDCPState(const ui::DisplaySnapshot& output,
                    const ui::GetHDCPStateCallback& callback) override;
  void SetHDCPState(const ui::DisplaySnapshot& output,
                    ui::HDCPState state,
                    const ui::SetHDCPStateCallback& callback) override;
  std::vector<ui::ColorCalibrationProfile> GetAvailableColorCalibrationProfiles(
      const ui::DisplaySnapshot& output) override;
  bool SetColorCalibrationProfile(
      const ui::DisplaySnapshot& output,
      ui::ColorCalibrationProfile new_profile) override;
  bool SetColorCorrection(const ui::DisplaySnapshot& output,
                          const std::vector<ui::GammaRampRGBEntry>& degamma_lut,
                          const std::vector<ui::GammaRampRGBEntry>& gamma_lut,
                          const std::vector<float>& correction_matrix) override;
  void AddObserver(ui::NativeDisplayObserver* observer) override;
  void RemoveObserver(ui::NativeDisplayObserver* observer) override;
  FakeDisplayController* GetFakeDisplayController() override;

 protected:
  // Sets initial display snapshots from command line flag. Returns true if
  // command line flag was provided.
  bool InitFromCommandLine();

  // Updates observers when display configuration has changed. Will not update
  // until after |Initialize()| has been called.
  void OnConfigurationChanged();

 private:
  base::ObserverList<ui::NativeDisplayObserver> observers_;
  std::vector<std::unique_ptr<ui::DisplaySnapshot>> displays_;

  // If |Initialize()| has been called.
  bool initialized_ = false;

  // The next available display id.
  uint8_t next_display_id_ = 0;

  DISALLOW_COPY_AND_ASSIGN(FakeDisplayDelegate);
};

}  // namespace display
#endif  // UI_DISPLAY_FAKE_DISPLAY_DELEGATE_H_
