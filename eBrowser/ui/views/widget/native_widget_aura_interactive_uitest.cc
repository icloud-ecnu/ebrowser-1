// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/widget/native_widget_aura.h"

#include "base/path_service.h"
#include "ui/aura/window.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/gl/test/gl_surface_test_support.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/test/native_widget_factory.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/wm/core/base_focus_rules.h"
#include "ui/wm/core/focus_controller.h"

namespace views {
namespace test {

class TestFocusRules : public wm::BaseFocusRules {
 public:
  TestFocusRules() {}
  ~TestFocusRules() override {}

  void set_can_activate(bool can_activate) { can_activate_ = can_activate; }

  // wm::BaseFocusRules overrides:
  bool SupportsChildActivation(aura::Window* window) const override {
    return true;
  }

  bool CanActivateWindow(aura::Window* window) const override {
    return can_activate_;
  }

 private:
  bool can_activate_ = true;

  DISALLOW_COPY_AND_ASSIGN(TestFocusRules);
};

class NativeWidgetAuraTest : public ViewsTestBase {
 public:
  NativeWidgetAuraTest() {}
  ~NativeWidgetAuraTest() override {}

  void SetUp() override {
    gl::GLSurfaceTestSupport::InitializeOneOff();
    ui::RegisterPathProvider();
    base::FilePath ui_test_pak_path;
    ASSERT_TRUE(PathService::Get(ui::UI_TEST_PAK, &ui_test_pak_path));
    ui::ResourceBundle::InitSharedInstanceWithPakPath(ui_test_pak_path);

    ViewsTestBase::SetUp();
  }

  NativeWidget* CreateNativeWidget(const Widget::InitParams& params,
                                   Widget* widget) {
    return CreatePlatformNativeWidgetImpl(params, widget, kDefault, nullptr);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(NativeWidgetAuraTest);
};

// When requesting view focus from a non-active top level widget, focus is not
// instantly given. Instead, the view is firstly stored and then it is attempted
// to activate the widget. If widget is currently not activatable, focus should
// not be grabbed. And focus will be given/restored the next time the widget is
// made active. (crbug.com/621791)
TEST_F(NativeWidgetAuraTest, NonActiveWindowRequestImeFocus) {
  TestFocusRules* test_focus_rules = new TestFocusRules;
  std::unique_ptr<wm::FocusController> focus_controller =
      base::MakeUnique<wm::FocusController>(test_focus_rules);
  aura::client::SetActivationClient(GetContext(), focus_controller.get());

  Widget* widget1 = new Widget;
  Widget::InitParams params1(Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params1.context = GetContext();
  params1.native_widget = CreateNativeWidget(params1, widget1);
  params1.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  widget1->Init(params1);
  Textfield* textfield1 = new Textfield;
  widget1->GetRootView()->AddChildView(textfield1);

  Widget* widget2 = new Widget;
  Widget::InitParams params2(Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params2.context = GetContext();
  params2.native_widget = CreateNativeWidget(params2, widget2);
  params2.ownership = Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  widget2->Init(params2);
  Textfield* textfield2a = new Textfield;
  widget2->GetRootView()->AddChildView(textfield2a);
  Textfield* textfield2b = new Textfield;
  widget2->GetRootView()->AddChildView(textfield2b);

  views::test::WidgetActivationWaiter waiter1(widget1, true);
  widget1->Show();
  waiter1.Wait();
  textfield1->RequestFocus();
  EXPECT_TRUE(textfield1->HasFocus());
  EXPECT_FALSE(textfield2a->HasFocus());
  EXPECT_FALSE(textfield2b->HasFocus());

  // Don't allow window activation at this step.
  test_focus_rules->set_can_activate(false);
  textfield2a->RequestFocus();
  EXPECT_TRUE(textfield1->HasFocus());
  EXPECT_FALSE(textfield2a->HasFocus());
  EXPECT_FALSE(textfield2b->HasFocus());

  // Allow window activation and |widget2| gets activated at this step, focus
  // should be properly restored.
  test_focus_rules->set_can_activate(true);
  views::test::WidgetActivationWaiter waiter2(widget2, true);
  widget2->Activate();
  waiter2.Wait();
  EXPECT_TRUE(textfield2a->HasFocus());
  EXPECT_FALSE(textfield2b->HasFocus());
  EXPECT_FALSE(textfield1->HasFocus());

  widget1->CloseNow();
  widget2->CloseNow();
}

}  // namespace test
}  // namespace views
