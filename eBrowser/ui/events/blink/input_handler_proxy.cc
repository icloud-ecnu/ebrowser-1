// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "ui/events/blink/input_handler_proxy.h"

#include <stddef.h>

#include <algorithm>
#include <time.h>
#include "base/auto_reset.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/trace_event.h"
#include "cc/input/main_thread_scrolling_reason.h"
#include "third_party/WebKit/public/platform/WebInputEvent.h"
#include "ui/events/blink/did_overscroll_params.h"
#include "ui/events/blink/input_handler_proxy_client.h"
#include "ui/events/blink/input_scroll_elasticity_controller.h"
#include "ui/events/blink/web_input_event_traits.h"
#include "ui/events/latency_info.h"
#include "ui/gfx/geometry/point_conversions.h"
#include "ui/events/blink/svm.h"

using blink::WebFloatPoint;
using blink::WebFloatSize;
using blink::WebGestureEvent;
using blink::WebInputEvent;
using blink::WebMouseEvent;
using blink::WebMouseWheelEvent;
using blink::WebPoint;
using blink::WebTouchEvent;
using blink::WebTouchPoint;

namespace {

const int32_t kEventDispositionUndefined = -1;

// Maximum time between a fling event's timestamp and the first |Animate| call
// for the fling curve to use the fling timestamp as the initial animation time.
// Two frames allows a minor delay between event creation and the first animate.
const double kMaxSecondsFromFlingTimestampToFirstAnimate = 2. / 60.;

// Threshold for determining whether a fling scroll delta should have caused the
// client to scroll.
const float kScrollEpsilon = 0.1f;

// Minimum fling velocity required for the active fling and new fling for the
// two to accumulate.
const double kMinBoostFlingSpeedSquare = 350. * 350.;

// Minimum velocity for the active touch scroll to preserve (boost) an activef
// fling for which cancellation has been deferred.
const double kMinBoostTouchScrollSpeedSquare = 150 * 150.;

// Timeout window after which the active fling will be cancelled if no animation
// ticks, scrolls or flings of sufficient velocity relative to the current fling
// are received. The default value on Android native views is 40ms, but we use a
// slightly increased value to accomodate small IPC message delays.
const double kFlingBoostTimeoutDelaySeconds = 0.05;


//-----------------modify start
double start_point_x = 0;
double start_point_y = 0;

double start_point_x_ = 0;
double start_point_y_ = 0;

double accumulated_delta_x = 0;
double accumulated_delta_y = 0;

double accumulated_delta_x_ = 0;
double accumulated_delta_y_ = 0;
/*
float  accumulated_scale = 1;
double pinch_point_x = 0;
double pinch_point_y = 0;*/
int pinchCount = 0;
int scrollCount = 0;
int fps = 0;
bool isPinch = false;
//WebGestureEvent update_event;
//float vy = 0;




//----------------modify end
gfx::Vector2dF ToClientScrollIncrement(const WebFloatSize& increment) {
  return gfx::Vector2dF(-increment.width, -increment.height);
}

double InSecondsF(const base::TimeTicks& time) {
  return (time - base::TimeTicks()).InSecondsF();
}

bool ShouldSuppressScrollForFlingBoosting(
    const gfx::Vector2dF& current_fling_velocity,
    const WebGestureEvent& scroll_update_event,
    double time_since_last_boost_event,
    double time_since_last_fling_animate) {

  //return false;//----------------------------------------------
  DCHECK_EQ(WebInputEvent::GestureScrollUpdate, scroll_update_event.type);

  gfx::Vector2dF dx(scroll_update_event.data.scrollUpdate.deltaX,
                    scroll_update_event.data.scrollUpdate.deltaY);
  if (gfx::DotProduct(current_fling_velocity, dx) <= 0)
    return false;

  if (time_since_last_fling_animate > kFlingBoostTimeoutDelaySeconds)
    return false;

  if (time_since_last_boost_event < 0.001)
    return true;

  // TODO(jdduke): Use |scroll_update_event.data.scrollUpdate.velocity{X,Y}|.
  // The scroll must be of sufficient velocity to maintain the active fling.
  const gfx::Vector2dF scroll_velocity =
      gfx::ScaleVector2d(dx, 1. / time_since_last_boost_event);
  if (scroll_velocity.LengthSquared() < kMinBoostTouchScrollSpeedSquare)
    return false;

  return true;
}

bool ShouldBoostFling(const gfx::Vector2dF& current_fling_velocity,
                      const WebGestureEvent& fling_start_event) {
  //return false;
  DCHECK_EQ(WebInputEvent::GestureFlingStart, fling_start_event.type);

  gfx::Vector2dF new_fling_velocity(
      fling_start_event.data.flingStart.velocityX,
      fling_start_event.data.flingStart.velocityY);

  if (gfx::DotProduct(current_fling_velocity, new_fling_velocity) <= 0)
    return false;

  if (current_fling_velocity.LengthSquared() < kMinBoostFlingSpeedSquare)
    return false;

  if (new_fling_velocity.LengthSquared() < kMinBoostFlingSpeedSquare)
    return false;

  return true;
}

//用当前的event构建一个GestureScrollBegin的event
WebGestureEvent ObtainGestureScrollBegin(const WebGestureEvent& event) {
  WebGestureEvent scroll_begin_event = event;
  scroll_begin_event.type = WebInputEvent::GestureScrollBegin;
  scroll_begin_event.data.scrollBegin.deltaXHint = 0;
  scroll_begin_event.data.scrollBegin.deltaYHint = 0;
  return scroll_begin_event;
}
//创建ScrollState
cc::ScrollState CreateScrollStateForGesture(const WebGestureEvent& event) {
  cc::ScrollStateData scroll_state_data;
  //LOG(INFO)<<"CreateScrollStateForGesture------------";
  switch (event.type) {
    case WebInputEvent::GestureScrollBegin:
      scroll_state_data.position_x = event.x;
      scroll_state_data.position_y = event.y;
      scroll_state_data.is_beginning = true;
      // On Mac, a GestureScrollBegin in the inertial phase indicates a fling
      // start.
      if (event.data.scrollBegin.inertialPhase ==
          WebGestureEvent::MomentumPhase) {
        scroll_state_data.is_in_inertial_phase = true;
      }
      break;
    case WebInputEvent::GestureFlingStart:
      scroll_state_data.velocity_x = event.data.flingStart.velocityX;//event.data.flingStart.velocityX
      scroll_state_data.velocity_y = event.data.flingStart.velocityY;//
      scroll_state_data.is_in_inertial_phase = true;
      break;
    case WebInputEvent::GestureScrollUpdate:
      scroll_state_data.delta_x = -event.data.scrollUpdate.deltaX;//-------------------重点
      scroll_state_data.delta_y = -event.data.scrollUpdate.deltaY;
      scroll_state_data.velocity_x = event.data.scrollUpdate.velocityX;
      scroll_state_data.velocity_y = event.data.scrollUpdate.velocityY;
      scroll_state_data.is_in_inertial_phase =
          event.data.scrollUpdate.inertialPhase ==
          WebGestureEvent::MomentumPhase;
      break;
    case WebInputEvent::GestureScrollEnd:
    case WebInputEvent::GestureFlingCancel:
      scroll_state_data.is_ending = true;
      break;
    default:
      NOTREACHED();
      break;
  }
  return cc::ScrollState(scroll_state_data);
}

void ReportInputEventLatencyUma(const WebInputEvent& event,
                                const ui::LatencyInfo& latency_info) {
  if (!(event.type == WebInputEvent::GestureScrollBegin ||
        event.type == WebInputEvent::GestureScrollUpdate ||
        event.type == WebInputEvent::GesturePinchBegin ||
        event.type == WebInputEvent::GesturePinchUpdate ||
        event.type == WebInputEvent::GestureFlingStart)) {
    return;
  }
  //LOG(INFO)<<"ReportInputEventLatencyUma----------------------";
  ui::LatencyInfo::LatencyMap::const_iterator it =
      latency_info.latency_components().find(std::make_pair(
          ui::INPUT_EVENT_LATENCY_ORIGINAL_COMPONENT, 0));

  if (it == latency_info.latency_components().end())
    return;

  base::TimeDelta delta = base::TimeTicks::Now() - it->second.event_time;
  for (size_t i = 0; i < it->second.event_count; ++i) {
    switch (event.type) {
      case blink::WebInputEvent::GestureScrollBegin:
        UMA_HISTOGRAM_CUSTOM_COUNTS(
            "Event.Latency.RendererImpl.GestureScrollBegin",
            delta.InMicroseconds(), 1, 1000000, 100);
        break;
      case blink::WebInputEvent::GestureScrollUpdate:
        UMA_HISTOGRAM_CUSTOM_COUNTS(
            // So named for historical reasons.
            "Event.Latency.RendererImpl.GestureScroll2",
            delta.InMicroseconds(), 1, 1000000, 100);
        break;
      case blink::WebInputEvent::GesturePinchBegin:
        UMA_HISTOGRAM_CUSTOM_COUNTS(
            "Event.Latency.RendererImpl.GesturePinchBegin",
            delta.InMicroseconds(), 1, 1000000, 100);
        break;
      case blink::WebInputEvent::GesturePinchUpdate:
        UMA_HISTOGRAM_CUSTOM_COUNTS(
            "Event.Latency.RendererImpl.GesturePinchUpdate",
            delta.InMicroseconds(), 1, 1000000, 100);
        break;
      case blink::WebInputEvent::GestureFlingStart:
        UMA_HISTOGRAM_CUSTOM_COUNTS(
            "Event.Latency.RendererImpl.GestureFlingStart",
            delta.InMicroseconds(), 1, 1000000, 100);
        break;
      default:
        NOTREACHED();
        break;
    }
  }
}
//滚轮输入还是触摸屏输入
cc::InputHandler::ScrollInputType GestureScrollInputType(
    blink::WebGestureDevice device) {
  return device == blink::WebGestureDeviceTouchpad
             ? cc::InputHandler::WHEEL
             : cc::InputHandler::TOUCHSCREEN;
}

}  // namespace

namespace ui {

InputHandlerProxy::InputHandlerProxy(cc::InputHandler* input_handler,
                                     InputHandlerProxyClient* client)
    : client_(client), //ThreadProxy
      input_handler_(input_handler), //LayerTreeHostImpl
      deferred_fling_cancel_time_seconds_(0),
      synchronous_input_handler_(nullptr),
      allow_root_animate_(true),
#ifndef NDEBUG
      expect_scroll_update_end_(false),
#endif
      gesture_scroll_on_impl_thread_(false),
      gesture_pinch_on_impl_thread_(false),
      fling_may_be_active_on_main_thread_(false),
      disallow_horizontal_fling_scroll_(false),
      disallow_vertical_fling_scroll_(false),
      has_fling_animation_started_(false),
      smooth_scroll_enabled_(false),
      uma_latency_reporting_enabled_(base::TimeTicks::IsHighResolution()),
      touch_start_result_(kEventDispositionUndefined),
      current_overscroll_params_(nullptr) {
  DCHECK(client);
  input_handler_->BindToClient(this);
  cc::ScrollElasticityHelper* scroll_elasticity_helper =
      input_handler_->CreateScrollElasticityHelper();
  if (scroll_elasticity_helper) {
    scroll_elasticity_controller_.reset(
        new InputScrollElasticityController(scroll_elasticity_helper));
  }
}

InputHandlerProxy::~InputHandlerProxy() {}

void InputHandlerProxy::WillShutdown() {
  scroll_elasticity_controller_.reset();
  input_handler_ = NULL;
  client_->WillShutdown();
}


//svm


std::string msg_model;
int routing_id_ = 0;
int scrollSpeed = 0;
int countScrolling = 0;
int countPinching = 0;
std::string webviewmodel = "svm_type epsilon_svr\n"
"kernel_type rbf\n"
"gamma 0.1\n"
"nr_class 2\n"
"total_sv 31\n"
"rho -30.064\n"
"SV\n"
"-375.1906993292752 1:0.4\n" 
"-1000 1:0.5\n" 
"76.88789430911002 1:0.9\n" 
"1000 1:1\n" 
"1000 1:1.1\n" 
"1000 1:1.3\n" 
"-1000 1:1.5\n" 
"-1000 1:2\n" 
"152.4244333825901 1:2.8\n" 
"-658.9273041510949 1:3.6\n" 
"1000 1:4\n" 
"110.2583412231241 1:4.8\n" 
"-1000 1:5.6\n" 
"1000 1:6\n" 
"-1000 1:6.8\n" 
"497.4609352251786 1:7.6\n" 
"1000 1:8\n" 
"-1000 1:8.8\n" 
"-816.4934305923082 1:9.6\n" 
"1000 1:10\n" 
"471.5388027392294 1:11\n" 
"-1000 1:12\n" 
"771.8231997159018 1:13\n" 
"-276.2898550887135 1:14\n" 
"36.63110800575065 1:16\n" 
"6.134237008088796 1:18\n" 
"-13.2777632498265 1:20\n" 
"16.39637833119732 1:21\n" 
"-2.474188188578108 1:23\n" 
"3.300645387053644 1:25\n" 
"-0.202734727425062 1:28";

void InputHandlerProxy::HandleInputModelMsg(std::string msg,int routing_id,int speed){
    LOG(INFO)<<"HandleInputModelMsg-------------------"<<msg;
    routing_id_ = routing_id;
    if(msg!=""&&msg.length()!=0){msg_model = msg;}
    speed = speed==0?200:speed;
    scrollSpeed = speed;
    LOG(INFO)<<"Speed now: "<<scrollSpeed;
}


void InputHandlerProxy::HandleInputEventWithLatencyInfo(
    ScopedWebInputEvent event,
    const LatencyInfo& latency_info,
    const EventDispositionCallback& callback) {
  DCHECK(input_handler_);
  //LOG(INFO)<<"HandleInputEventWithLatencyInfo-------------------";
  if (uma_latency_reporting_enabled_)
    ReportInputEventLatencyUma(*event, latency_info);

  TRACE_EVENT_WITH_FLOW1("input,benchmark", "LatencyInfo.Flow",
                         TRACE_ID_DONT_MANGLE(latency_info.trace_id()),
                         TRACE_EVENT_FLAG_FLOW_IN | TRACE_EVENT_FLAG_FLOW_OUT,
                         "step", "HandleInputEventImpl");

  ui::LatencyInfo monitored_latency_info = latency_info;
  std::unique_ptr<cc::SwapPromiseMonitor> latency_info_swap_promise_monitor =
      input_handler_->CreateLatencyInfoSwapPromiseMonitor(
          &monitored_latency_info);

  current_overscroll_params_.reset();
  //主要是将参数event描述的输入事件分给另外一个成员函数HandleInputEvent处理
  InputHandlerProxy::EventDisposition disposition = HandleInputEvent(*event);
  callback.Run(disposition, std::move(event), monitored_latency_info,
               std::move(current_overscroll_params_));
}

InputHandlerProxy::EventDisposition InputHandlerProxy::HandleInputEvent(
    const WebInputEvent& event) {

  DCHECK(input_handler_);
  //LOG(INFO)<<"InputHandlerProxy::HandleInputEvent--------distribute a handle function to event";

  if (FilterInputEventForFlingBoosting(event))//?true
    return DID_HANDLE;

  switch (event.type) {
    case WebInputEvent::MouseWheel:
      return HandleMouseWheel(static_cast<const WebMouseWheelEvent&>(event));

    case WebInputEvent::GestureScrollBegin:
      return HandleGestureScrollBegin(
          static_cast<const WebGestureEvent&>(event));

    case WebInputEvent::GestureScrollUpdate:
          scrollCount += 1;
          //LOG(INFO)<<"滚动次数："<<scrollCount;
          
          //base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(24163));
      return HandleGestureScrollUpdate(static_cast<const WebGestureEvent&>(event));

    case WebInputEvent::GestureScrollEnd:
      return HandleGestureScrollEnd(static_cast<const WebGestureEvent&>(event));

    case WebInputEvent::GesturePinchBegin: {
      //----------start
	isPinch = true;
      //----------end
      DCHECK(!gesture_pinch_on_impl_thread_);
      //WebInputEvent => WebGestureEvent
      const WebGestureEvent& gesture_event =
          static_cast<const WebGestureEvent&>(event);
      //看else不看if
      if (gesture_event.sourceDevice == blink::WebGestureDeviceTouchpad &&
          input_handler_->GetEventListenerProperties(
              cc::EventListenerClass::kMouseWheel) !=
              cc::EventListenerProperties::kNone) {
        return DID_NOT_HANDLE;
      } else {
	//LOG(INFO)<<"+++++++++++++++++++++++++++++++++GesturePinchBegin";
        input_handler_->PinchGestureBegin();
        gesture_pinch_on_impl_thread_ = true;
        return DID_HANDLE;
      }
    }

    case WebInputEvent::GesturePinchEnd:
      //----------start
	isPinch = false;
      //----------end
      if (gesture_pinch_on_impl_thread_) {
        gesture_pinch_on_impl_thread_ = false;
	pinchCount = 0;
	//LOG(INFO)<<"+++++++++++++++++++++++++++++++++GesturePinchEnd";
//---
	/*input_handler_->PinchGestureUpdate(accumulated_scale,gfx::Point(pinch_point_x, pinch_point_y));
	accumulated_scale = 1;*/
//---
        input_handler_->PinchGestureEnd();
        return DID_HANDLE;
      } else {
        return DID_NOT_HANDLE;
      }

    case WebInputEvent::GesturePinchUpdate: {
      if (gesture_pinch_on_impl_thread_) {
      double fps = 0.0213*scrollSpeed+15.6;
      scrollSpeed = 0;
      //LOG(INFO)<<"+++++++++++++++++++++++++++++PinchSpeed:"<<scrollSpeed;
      fps = ceil(fps);
      //LOG(INFO)<<"+++++++++++++++++++++++++++++fps-predict:"<<fps;
      if(fps<10){fps = 10;}if(fps>60){fps = 60;}
      countPinching++;
      //LOG(INFO)<<"+++++++++++++++++++++++++++++pinch-fps:"<<fps;
      //LOG(INFO)<<"+++++++++++++++++++++++++++++pinch-predict-fps:"<<fps<<" Time:"<<time(NULL)<<" count: "<<countPinching;
      if(fps >= 1 && fps <= 9){
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(1000000/fps - 16667));
        //LOG(INFO)<<"当前pinch-fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(1000000/fps - 16667);
      }

      if(fps >= 10 && fps <= 15){
        base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(1000000/fps - 16667+(fps-10)*1667*0.75));
        //LOG(INFO)<<"当前pinch-fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(1000000/fps - 16667+(fps-10)*1667*0.75);
      }
      else if(fps > 15 && fps < 20){
	
        base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(1000000/fps - 16667+(fps-10)*1667*0.6));
        //LOG(INFO)<<"当前pinch-fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(1000000/fps - 16667+(fps-10)*1667*0.6);//0.4
      }       
      else if(fps >=20 && fps <= 30){
	//base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(24997));
        base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(24997+1667*(30-fps)));
        //LOG(INFO)<<"当前pinch-fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(24997+1667*(30-fps));
      }
      else if(fps >30 && fps<=41){
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(16663+1667*(41-fps)*0.5));
       //LOG(INFO)<<"当前pinch-fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(16663+1667*(41-fps)*0.5);
      }

      else if(fps>41 && fps<=44){
       //base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(24997+1667*(30-fps)));
       //base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(1000000/fps - 16667));
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(16246+1667*(44-fps)*0.5));
       //LOG(INFO)<<"当前pinch-fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(16246+1667*(44-fps)*0.5);
      } //till 45
      else if(fps==45){
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(15412));
       //LOG(INFO)<<"当前fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(15412);
      }
      else if(fps>45 && fps <=52){
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(11245+1667*(52-fps)*0.5));
       //LOG(INFO)<<"当前pinch-fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(11245+1667*(52-fps)*0.5);
      }
      else if(fps==55){//52 15412
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(12912));
       //LOG(INFO)<<"当前pinch-fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(12912);
      }
     else if(fps == 60){
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(0));
       //LOG(INFO)<<"当前pinch-fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(0);
      }
      else{
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(12912+1667*(55-fps)*0.5));
       //LOG(INFO)<<"当前pinch-fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(12912+1667*(55-fps)*0.5);
      }
	pinchCount++;
	//LOG(INFO)<<pinchCount<<"+++++++++++++++++++++++++++++++++GesturePinchUpdate";
	/*
	if(pinchCount){
		return DROP_EVENT;	
	}*/
        const WebGestureEvent& gesture_event = static_cast<const WebGestureEvent&>(event);
        if (gesture_event.data.pinchUpdate.zoomDisabled)
            return DROP_EVENT;

	//把参数累计一下，在end事件中调用
	/*LOG(INFO)<<"scale:"<<gesture_event.data.pinchUpdate.scale<<"-x:"<<gesture_event.x<<"-y:"<<gesture_event.y;
	accumulated_scale *= gesture_event.data.pinchUpdate.scale;
	pinch_point_x = gesture_event.x;
	pinch_point_y = gesture_event.y;
	LOG(INFO)<<"accumulated_scale:"<<accumulated_scale;*/
	//below is initial source code
        //LOG(INFO)<<pinchCount<<"+++++++++++++++++++++++++++++++++GesturePinchUpdate-scale: "<<gesture_event.data.pinchUpdate.scale;
	input_handler_->PinchGestureUpdate(gesture_event.data.pinchUpdate.scale,gfx::Point(gesture_event.x, gesture_event.y));

        return DID_HANDLE;
      } 
	else {
        return DID_NOT_HANDLE;
      }
    }

    case WebInputEvent::GestureFlingStart:
      //
      #ifndef NDEBUG
  expect_scroll_update_end_ = false;
#endif
      //
      //LOG(INFO)<<"HandleGestureFlingStart";return DID_NOT_HANDLE;
/*
      return HandleGestureFlingStart(
          *static_cast<const WebGestureEvent*>(&event));*/

    case WebInputEvent::GestureFlingCancel:
      if (CancelCurrentFling())
        return DID_HANDLE;
      else if (!fling_may_be_active_on_main_thread_)
        return DROP_EVENT;
      return DID_NOT_HANDLE;

    case WebInputEvent::TouchStart:
      return HandleTouchStart(static_cast<const WebTouchEvent&>(event));

    case WebInputEvent::TouchMove:
      return HandleTouchMove(static_cast<const WebTouchEvent&>(event));

    case WebInputEvent::TouchEnd:
      return HandleTouchEnd(static_cast<const WebTouchEvent&>(event));

    case WebInputEvent::MouseDown: {
      // Only for check scrollbar captured
      const WebMouseEvent& mouse_event =
          static_cast<const WebMouseEvent&>(event);

      if (mouse_event.button == blink::WebMouseEvent::Button::Left) {
        CHECK(input_handler_);
        input_handler_->MouseDown();
      }
      return DID_NOT_HANDLE;
    }
    case WebInputEvent::MouseUp: {
      // Only for release scrollbar captured
      const WebMouseEvent& mouse_event =
          static_cast<const WebMouseEvent&>(event);

      if (mouse_event.button == blink::WebMouseEvent::Button::Left) {
        CHECK(input_handler_);
        input_handler_->MouseUp();
      }
      return DID_NOT_HANDLE;
    }
    case WebInputEvent::MouseMove: {
      const WebMouseEvent& mouse_event =
          static_cast<const WebMouseEvent&>(event);
      // TODO(davemoore): This should never happen, but bug #326635 showed some
      // surprising crashes.
      CHECK(input_handler_);
      input_handler_->MouseMoveAt(gfx::Point(mouse_event.x, mouse_event.y));
      return DID_NOT_HANDLE;
    }
    case WebInputEvent::MouseLeave: {
      CHECK(input_handler_);
      input_handler_->MouseLeave();
      return DID_NOT_HANDLE;
    }

    default:
      if (WebInputEvent::isKeyboardEventType(event.type)) {
        // Only call |CancelCurrentFling()| if a fling was active, as it will
        // otherwise disrupt an in-progress touch scroll.
        if (fling_curve_)
          CancelCurrentFling();
      }
      break;
  }

  return DID_NOT_HANDLE;
}

void InputHandlerProxy::RecordMainThreadScrollingReasons(
    blink::WebGestureDevice device,
    uint32_t reasons) {
  static const char* kGestureHistogramName =
      "Renderer4.MainThreadGestureScrollReason";
  static const char* kWheelHistogramName =
      "Renderer4.MainThreadWheelScrollReason";

  DCHECK(device == blink::WebGestureDeviceTouchpad ||
         device == blink::WebGestureDeviceTouchscreen);

  if (device != blink::WebGestureDeviceTouchpad &&
      device != blink::WebGestureDeviceTouchscreen) {
    return;
  }

  if (reasons == cc::MainThreadScrollingReason::kNotScrollingOnMain) {
    if (device == blink::WebGestureDeviceTouchscreen) {
      UMA_HISTOGRAM_ENUMERATION(
          kGestureHistogramName,
          cc::MainThreadScrollingReason::kNotScrollingOnMain,
          cc::MainThreadScrollingReason::kMainThreadScrollingReasonCount);
    } else {
      UMA_HISTOGRAM_ENUMERATION(
          kWheelHistogramName,
          cc::MainThreadScrollingReason::kNotScrollingOnMain,
          cc::MainThreadScrollingReason::kMainThreadScrollingReasonCount);
    }
  }

  for (uint32_t i = 0;
       i < cc::MainThreadScrollingReason::kMainThreadScrollingReasonCount - 1;
       ++i) {
    unsigned val = 1 << i;
    if (reasons & val) {
      if (val == cc::MainThreadScrollingReason::kHandlingScrollFromMainThread) {
        // We only want to record "Handling scroll from main thread" reason if
        // it's the only reason. If it's not the only reason, the "real" reason
        // for scrolling on main is something else, and we only want to pay
        // attention to that reason.
        if (reasons & ~val)
          continue;
      }
      if (device == blink::WebGestureDeviceTouchscreen) {
        UMA_HISTOGRAM_ENUMERATION(
            kGestureHistogramName, i + 1,
            cc::MainThreadScrollingReason::kMainThreadScrollingReasonCount);
      } else {
        UMA_HISTOGRAM_ENUMERATION(
            kWheelHistogramName, i + 1,
            cc::MainThreadScrollingReason::kMainThreadScrollingReasonCount);
      }
    }
  }
}

bool InputHandlerProxy::ShouldAnimate(bool has_precise_scroll_deltas) const {
#if defined(OS_MACOSX)
  // Mac does not smooth scroll wheel events (crbug.com/574283).
  return false;
#else
  return smooth_scroll_enabled_ && !has_precise_scroll_deltas;
#endif
}

InputHandlerProxy::EventDisposition InputHandlerProxy::HandleMouseWheel(
    const WebMouseWheelEvent& wheel_event) {
  // Only call |CancelCurrentFling()| if a fling was active, as it will
  // otherwise disrupt an in-progress touch scroll.
  if (!wheel_event.hasPreciseScrollingDeltas && fling_curve_)
    CancelCurrentFling();

    cc::EventListenerProperties properties =
        input_handler_->GetEventListenerProperties(
            cc::EventListenerClass::kMouseWheel);
    switch (properties) {
      case cc::EventListenerProperties::kPassive:
        return DID_HANDLE_NON_BLOCKING;
      case cc::EventListenerProperties::kBlockingAndPassive:
      case cc::EventListenerProperties::kBlocking:
        return DID_NOT_HANDLE;
      case cc::EventListenerProperties::kNone:
        return DROP_EVENT;
      default:
        NOTREACHED();
        return DROP_EVENT;
    }
}

InputHandlerProxy::EventDisposition InputHandlerProxy::ScrollByMouseWheel(
    const WebMouseWheelEvent& wheel_event,
    cc::EventListenerProperties listener_properties) {
  DCHECK(listener_properties == cc::EventListenerProperties::kPassive ||
         listener_properties == cc::EventListenerProperties::kNone);

  // TODO(ccameron): The rail information should be pushed down into
  // InputHandler.
  gfx::Vector2dF scroll_delta(
      wheel_event.railsMode != WebInputEvent::RailsModeVertical
          ? -wheel_event.deltaX
          : 0,
      wheel_event.railsMode != WebInputEvent::RailsModeHorizontal
          ? -wheel_event.deltaY
          : 0);

  if (wheel_event.scrollByPage) {
    // TODO(jamesr): We don't properly handle scroll by page in the compositor
    // thread, so punt it to the main thread. http://crbug.com/236639
    RecordMainThreadScrollingReasons(
        blink::WebGestureDeviceTouchpad,
        cc::MainThreadScrollingReason::kPageBasedScrolling);
    return DID_NOT_HANDLE;
  } else {
    DCHECK(!ShouldAnimate(wheel_event.hasPreciseScrollingDeltas));
    cc::ScrollStateData scroll_state_begin_data;
    scroll_state_begin_data.position_x = wheel_event.x;
    scroll_state_begin_data.position_y = wheel_event.y;
    scroll_state_begin_data.is_beginning = true;
    cc::ScrollState scroll_state_begin(scroll_state_begin_data);
    cc::InputHandler::ScrollStatus scroll_status = input_handler_->ScrollBegin(
        &scroll_state_begin, cc::InputHandler::WHEEL);

    RecordMainThreadScrollingReasons(
        blink::WebGestureDeviceTouchpad,
        scroll_status.main_thread_scrolling_reasons);

    switch (scroll_status.thread) {
      case cc::InputHandler::SCROLL_ON_IMPL_THREAD: {
        TRACE_EVENT_INSTANT2("input",
                             "InputHandlerProxy::handle_input wheel scroll",
                             TRACE_EVENT_SCOPE_THREAD, "deltaX",
                             scroll_delta.x(), "deltaY", scroll_delta.y());

        cc::ScrollStateData scroll_state_update_data;
        scroll_state_update_data.delta_x = scroll_delta.x();
        scroll_state_update_data.delta_y = scroll_delta.y();
        scroll_state_update_data.position_x = wheel_event.x;
        scroll_state_update_data.position_y = wheel_event.y;
        cc::ScrollState scroll_state_update(scroll_state_update_data);

        cc::InputHandlerScrollResult scroll_result =
            input_handler_->ScrollBy(&scroll_state_update);
        HandleOverscroll(gfx::Point(wheel_event.x, wheel_event.y),
                         scroll_result, false);

        cc::ScrollStateData scroll_state_end_data;
        scroll_state_end_data.is_ending = true;
        cc::ScrollState scroll_state_end(scroll_state_end_data);
        input_handler_->ScrollEnd(&scroll_state_end);

        if (scroll_result.did_scroll) {
          return listener_properties == cc::EventListenerProperties::kPassive
                     ? DID_HANDLE_NON_BLOCKING
                     : DID_HANDLE;
        }
        return DROP_EVENT;
      }
      case cc::InputHandler::SCROLL_IGNORED:
        // TODO(jamesr): This should be DROP_EVENT, but in cases where we fail
        // to properly sync scrollability it's safer to send the event to the
        // main thread. Change back to DROP_EVENT once we have synchronization
        // bugs sorted out.
        return DID_NOT_HANDLE;
      case cc::InputHandler::SCROLL_UNKNOWN:
      case cc::InputHandler::SCROLL_ON_MAIN_THREAD:
        return DID_NOT_HANDLE;
      default:
        NOTREACHED();
        return DID_NOT_HANDLE;
    }
  }
}
//滚动开始
InputHandlerProxy::EventDisposition InputHandlerProxy::HandleGestureScrollBegin(
    const WebGestureEvent& gesture_event) {
  //---------start
  /*
  if(isPinch){
    return DROP_EVENT;
  }
  */
  //LOG(INFO)<<"ScrollBegin---------HandleGestureScrollBegin"<<gesture_event.x<<gesture_event.y;
  if((gesture_event.x ==-1&&gesture_event.y==-1)){
  	fps = gesture_event.timeStampSeconds*1000;
       // LOG(INFO)<<"---------ScrollBegin--X"<<gesture_event.x<<"---Y---"<<gesture_event.y<<"fps:"<<fps;
        return DROP_EVENT;
  }
  if((gesture_event.x ==0&&gesture_event.y==0)){
  	fps = gesture_event.timeStampSeconds*1000;
        //LOG(INFO)<<"---------ScrollBegin--X"<<gesture_event.x<<"---Y---"<<gesture_event.y<<"fps:"<<fps;
        return DROP_EVENT;
  }
  //---------end
  if (gesture_scroll_on_impl_thread_)
    CancelCurrentFling();

  //LOG(INFO)<<"ScrollBegin---------HandleGestureScrollBegin"<<time(NULL);
  

  //------modify start
  start_point_x = gesture_event.x;
  start_point_y = gesture_event.y;
  //------modify end

#ifndef NDEBUG
  DCHECK(!expect_scroll_update_end_);
  expect_scroll_update_end_ = true;
#endif
  cc::ScrollState scroll_state = CreateScrollStateForGesture(gesture_event);
  cc::InputHandler::ScrollStatus scroll_status;
  if (gesture_event.data.scrollBegin.deltaHintUnits ==
      blink::WebGestureEvent::ScrollUnits::Page) { //page (visible viewport) based scrolling
    scroll_status.thread = cc::InputHandler::SCROLL_ON_MAIN_THREAD;
    scroll_status.main_thread_scrolling_reasons =
        cc::MainThreadScrollingReason::kContinuingMainThreadScroll;
  } else if (gesture_event.data.scrollBegin.targetViewport) {//scroll the viewport if true
    scroll_status = input_handler_->RootScrollBegin(
        &scroll_state, GestureScrollInputType(gesture_event.sourceDevice));
  } else if (ShouldAnimate(gesture_event.data.scrollBegin.deltaHintUnits !=
                           blink::WebGestureEvent::ScrollUnits::Pixels)) {//large pixel jump should animate to delta
    DCHECK(!scroll_state.is_in_inertial_phase());
    //LOG(INFO)<<"---------ScrollAnimatedBegin--X"<<gesture_event.x<<"---Y---"<<gesture_event.y;
    gfx::Point scroll_point(gesture_event.x, gesture_event.y);
    scroll_status = input_handler_->ScrollAnimatedBegin(scroll_point);
  } else {
    //LOG(INFO)<<"---------滚动非Page非Viewport非Animate--X"<<gesture_event.x<<"---Y---"<<gesture_event.y;
    scroll_status = input_handler_->ScrollBegin(
        &scroll_state, GestureScrollInputType(gesture_event.sourceDevice));
  }
  UMA_HISTOGRAM_ENUMERATION("Renderer4.CompositorScrollHitTestResult",
                            scroll_status.thread,
                            cc::InputHandler::LAST_SCROLL_STATUS + 1);

  RecordMainThreadScrollingReasons(gesture_event.sourceDevice,
                                   scroll_status.main_thread_scrolling_reasons);

  InputHandlerProxy::EventDisposition result = DID_NOT_HANDLE;
  switch (scroll_status.thread) {
    case cc::InputHandler::SCROLL_ON_IMPL_THREAD:
      TRACE_EVENT_INSTANT0("input",
                           "InputHandlerProxy::handle_input gesture scroll",
                           TRACE_EVENT_SCOPE_THREAD);
      gesture_scroll_on_impl_thread_ = true;
      result = DID_HANDLE;
      break;
    case cc::InputHandler::SCROLL_UNKNOWN:
    case cc::InputHandler::SCROLL_ON_MAIN_THREAD:
      result = DID_NOT_HANDLE;
      break;
    case cc::InputHandler::SCROLL_IGNORED:
      result = DROP_EVENT;
      break;
  }
  if (scroll_elasticity_controller_ && result != DID_NOT_HANDLE)
    HandleScrollElasticityOverscroll(gesture_event,
                                     cc::InputHandlerScrollResult());

  return result;
}


double webview_predict(double speed)
{
     //LOG(INFO)<<"执行到了predict";
     //LOG(INFO)<<webviewmodel;
     //LOG(INFO)<<"执行到了predict-------------------"<<msg_model;
     /**如果msg_model不存在返回60*/
//----
    if(webviewmodel==""||webviewmodel.length()==0||webviewmodel=="stop"){return 60;}
//----
    struct svm_node *x;
    int max_nr_attr = 64;
    struct svm_model* model;
    int predict_probability=0;
    if((model=svm_load_model(webviewmodel.data()))==0)
    {
	LOG(INFO)<<"+++++++++++++++++++++++++++++fps30:"<<"can't load model string";
	exit(1);
    }

	x = (struct svm_node *) malloc(max_nr_attr*sizeof(struct svm_node));
	if(model==NULL){LOG(INFO)<<"模型不存在";}
	if(predict_probability)
	{
		if(svm_check_probability_model(model)==0)
		{
			LOG(INFO)<<"+++++++++++++++++++++++++++++fps30:"<<"Model does not support probabiliy estimates";
			exit(1);
		}
	}
	else
	{
		if(svm_check_probability_model(model)!=0)
		{
		LOG(INFO)<<"+++++++++++++++++++++++++++++fps30:"<<"Model supports probability estimates, but disabled in prediction";
		}			
	}
	//LOG(INFO)<<"执行到了predict";
        int i = 0;
        //double target_label, predict_label;
	double predict_label;
        //int inst_max_index = -1;
        //target_label = 0;
        x[i].index = 1;
        //inst_max_index = x[i].index;
        x[i].value = speed;
        ++i;
        x[i].index = -1;
	//LOG(INFO)<<"执行到了predict";
        predict_label = svm_predict(model,x);
	svm_free_and_destroy_model(&model);
        free(x);
        return predict_label;
}

double predict(double speed)
{
     LOG(INFO)<<"执行到了predict";
     //LOG(INFO)<<"执行到了predict-------------------"<<msg_model;
     /**如果msg_model不存在返回60*/
//----
    if(msg_model==""||msg_model.length()==0||msg_model=="stop"){return 60;}
//----
    struct svm_node *x;
    int max_nr_attr = 64;
    struct svm_model* model;
    int predict_probability=0;
    if((model=svm_load_model(msg_model.data()))==0)
    {
	LOG(INFO)<<"+++++++++++++++++++++++++++++fps30:"<<"can't load model string";
	exit(1);
    }
        LOG(INFO)<<"执行到了predict";
	x = (struct svm_node *) malloc(max_nr_attr*sizeof(struct svm_node));
	if(model==NULL){LOG(INFO)<<"模型不存在";}
	if(predict_probability)
	{
		if(svm_check_probability_model(model)==0)
		{
			LOG(INFO)<<"+++++++++++++++++++++++++++++fps30:"<<"Model does not support probabiliy estimates";
			exit(1);
		}
	}
	else
	{
		if(svm_check_probability_model(model)!=0)
		{
		LOG(INFO)<<"+++++++++++++++++++++++++++++fps30:"<<"Model supports probability estimates, but disabled in prediction";
		}			
	}
	
        int i = 0;
        //double target_label, predict_label;
	double predict_label;
        //int inst_max_index = -1;
        //target_label = 0;
        x[i].index = 1;
        //inst_max_index = x[i].index;
        x[i].value = speed;
        ++i;
        x[i].index = -1;
	LOG(INFO)<<"执行到了predict";
        predict_label = svm_predict(model,x);
	svm_free_and_destroy_model(&model);
        free(x);
        LOG(INFO)<<"执行到了predict"<<predict_label;
        return predict_label;
}

//滚动更新
int count = 0;
int last_fps  = 0;
InputHandlerProxy::EventDisposition
InputHandlerProxy::HandleGestureScrollUpdate(
    const WebGestureEvent& gesture_event) {
      
      //float v = fabs(gesture_event.data.flingStart.velocityY);
      //float v1 = fabs(gesture_event.data.scrollUpdate.velocityY);
      //float d1 = fabs(gesture_event.data.scrollUpdate.deltaY);
      //LOG(INFO)<<"当前速度:"<<v; LOG(INFO)<<"当前速度1:"<<v1; LOG(INFO)<<"当前delta:"<<d1;
      /***eScroll Start***/
      double fps;
      scrollSpeed = scrollSpeed/50;	
 //-----fps = 0.007419662329418*scrollSpeed*scrollSpeed*scrollSpeed-0.430964999468951*scrollSpeed*scrollSpeed+6.826023792939724*scrollSpeed+15.924633645316113;
      //fps = predict(fabs(v)/25);// cheng2/50 redmi
      //fps = 30;
	/*escroll*/
      fps = predict(scrollSpeed);//预测开关，开启Log关闭
      LOG(INFO)<<"-----------predict:"<<fps;
      //fps = predict(fabs(v)*25);
      //double fps = predict(fabs(v)*0.03);//c8816
      /*webview*/
      /*v = v*1000/16.67;
      scrollSpeed = v;
      fps = webview_predict(v/50);*/
      /**/
      /***e3x Start****/
      //v = fabs(v)*0.03;
      //double fps = 0.007419662329418*v*v*v-0.430964999468951*v*v+6.826023792939724*v+15.924633645316113;
      /***e3x End****/

      /***log Start****/
       //fps = 5.039*log(scrollSpeed)+5.928;
      /***log End****/

      //LOG(INFO)<<"+++++++++++++++++++++++++++++scrollSpeed:"<<scrollSpeed;
      fps = ceil(fps);

      //LOG(INFO)<<"+++++++++++++++++++++++++++++fps-predict:"<<fps;
      if(fps<10){fps = 10;}
      if(fps>60){fps = 60;}
      countScrolling++;
      LOG(INFO)<<"+++++++++++++++++++++++++++++scroll-fps:"<<fps;
      //LOG(INFO)<<"+++++++++++++++++++++++++++++pinch-predict-fps:"<<fps<<" Time:"<<time(NULL)<<" count: "<<countScrolling;
/*
      if(fps>last_fps){
      	last_fps = fps;
      }else{fps = last_fps;}

*/
      //LOG(INFO)<<"+++++++++++++++++++++++++++++fps-actual:"<<fps;
      if(fps >= 1 && fps <= 9){
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(1000000/fps - 16667));
        //LOG(INFO)<<"当前fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(1000000/fps - 16667);
      }

      if(fps >= 10 && fps <= 15){
        base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(1000000/fps - 16667+(fps-10)*1667*0.75));
        //LOG(INFO)<<"当前fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(1000000/fps - 16667+(fps-10)*1667*0.75);
      }
      else if(fps > 15 && fps < 20){
	
        base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(1000000/fps - 16667+(fps-10)*1667*0.6));
        //LOG(INFO)<<"当前fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(1000000/fps - 16667+(fps-10)*1667*0.6);//0.4
      }       
      else if(fps >=20 && fps <= 30){
	//base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(24997));
        base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(24997+1667*(30-fps)));
        //LOG(INFO)<<"当前fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(24997+1667*(30-fps));
      }
      else if(fps >30 && fps<=41){
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(16663+1667*(41-fps)*0.5));
       //LOG(INFO)<<"当前fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(16663+1667*(41-fps)*0.5);
      }

      else if(fps>41 && fps<=44){
       //base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(24997+1667*(30-fps)));
       //base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(1000000/fps - 16667));
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(16246+1667*(44-fps)*0.5));
       //LOG(INFO)<<"当前fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(16246+1667*(44-fps)*0.5);
      } //till 45
      else if(fps==45){
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(15412));
       //LOG(INFO)<<"当前fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(15412);
      }
      else if(fps>45 && fps <=52){
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(11245+1667*(52-fps)*0.5));
       //LOG(INFO)<<"当前fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(11245+1667*(52-fps)*0.5);
      }
      else if(fps==55){//52 15412
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(12912));
       //LOG(INFO)<<"当前fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(12912);
      }
     else if(fps == 60){
       //base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(0));
       LOG(INFO)<<"当前fps:60";
      }
      else{
       base::PlatformThread::Sleep(base::TimeDelta::FromMicroseconds(12912+1667*(55-fps)*0.5));
       //LOG(INFO)<<"当前fps:"<<fps<<"/delay:"<<base::TimeDelta::FromMicroseconds(12912+1667*(55-fps)*0.5);
      }
    
	/***eScroll End***/
	//return DROP_EVENT;
#ifndef NDEBUG
  DCHECK(expect_scroll_update_end_);
#endif
/*
      const float vx = gesture_event.data.flingStart.velocityX;
      const float vy = gesture_event.data.flingStart.velocityY;
      current_fling_velocity_ = gfx::Vector2dF(vx, vy);
      LOG(INFO)<<"Scroll速度vx:"<<vx<<"/vy:"<<vy;
*/
    //ce shi event delay
    //base::TimeTicks event_time =
    //    base::TimeTicks() +
     //   base::TimeDelta::FromSecondsD(gesture_event.timeStampSeconds);
    //base::TimeDelta delay = base::TimeTicks::Now() - event_time;
    //end
  //LOG(INFO)<<"ScrollUpdate-------------HandleGestureScrollUpdate+"<<++count<<"Time:"<<time(NULL)<<"Delay:"<<delay;
	//DLOG(INFO)<<"DLOG KEEP SCROLLING";
  if (!gesture_scroll_on_impl_thread_ && !gesture_pinch_on_impl_thread_)
    return DID_NOT_HANDLE;

  cc::ScrollState scroll_state = CreateScrollStateForGesture(gesture_event);
  gfx::Point scroll_point(gesture_event.x, gesture_event.y);
  gfx::Vector2dF scroll_delta(-gesture_event.data.scrollUpdate.deltaX,
                              -gesture_event.data.scrollUpdate.deltaY);
	//----------modify start
  if(start_point_x_ == 0){
    start_point_x_ = gesture_event.x; 	
  }
  if(start_point_y_ == 0){
    start_point_y_ = gesture_event.y; 	
  }
  
  accumulated_delta_x_  += (gesture_event.data.scrollUpdate.deltaX);	
  
  
  accumulated_delta_y_  += (gesture_event.data.scrollUpdate.deltaY);	
  
	accumulated_delta_x += (gesture_event.data.scrollUpdate.deltaX);
	accumulated_delta_y += (gesture_event.data.scrollUpdate.deltaY);
	//LOG(INFO)<<"-------------scroll_delta--y"<<gesture_event.data.scrollUpdate.deltaY<<"event_x:"<<gesture_event.x<<"event_y:"<<gesture_event.y;
        //LOG(INFO)<<"-------------scroll_delta--y"<<gesture_event.data.scrollUpdate.deltaY<<"start_point_x_:"<<start_point_x_<<"start_point_y_:"<<start_point_y_;
	//---------modify end


  if (ShouldAnimate(gesture_event.data.scrollUpdate.deltaUnits !=
                    blink::WebGestureEvent::ScrollUnits::Pixels)) {
    DCHECK(!scroll_state.is_in_inertial_phase());
    base::TimeTicks event_time =
        base::TimeTicks() +
        base::TimeDelta::FromSecondsD(gesture_event.timeStampSeconds);
    base::TimeDelta delay = base::TimeTicks::Now() - event_time;
    switch (input_handler_->ScrollAnimated(scroll_point, scroll_delta, delay)
                .thread) {
      case cc::InputHandler::SCROLL_ON_IMPL_THREAD:
        //LOG(INFO)<<"HandleGestureScrollUpdate-------------ShouldAnimate";
        return DID_HANDLE;
      case cc::InputHandler::SCROLL_IGNORED:
        return DROP_EVENT;
      default:
        return DID_NOT_HANDLE;
    }
  }


//----
/*
  if(count%60 != 0){
    return DROP_EVENT;
 }
*/

//----modify start
    /*cc::ScrollStateData scroll_state_data;
    scroll_state_data.position_x = start_point_x_;
    scroll_state_data.position_y = start_point_y_;
    
    scroll_state_data.delta_x = -accumulated_delta_x_;
    scroll_state_data.delta_y = -accumulated_delta_y_;
    LOG(INFO)<<"accumulated_delta_x_/accumulated_delta_y_"<<accumulated_delta_x_<<"/"<<accumulated_delta_y_;
    cc::ScrollState scroll_state_ = cc::ScrollState(scroll_state_data);
    gfx::Point scroll_point_(start_point_x_, start_point_y_);
    //scroll_state.set_is_ending(false);
   cc::InputHandlerScrollResult scroll_result =
      input_handler_->ScrollBy(&scroll_state_);
  HandleOverscroll(scroll_point_, scroll_result, true);
*/


//----
//---source code below

  cc::InputHandlerScrollResult scroll_result = input_handler_->ScrollBy(&scroll_state);
  HandleOverscroll(scroll_point, scroll_result, true);
  
  if (scroll_elasticity_controller_)
    HandleScrollElasticityOverscroll(gesture_event, scroll_result);
  //LOG(INFO)<<"ScrollUpdate-------------调用"<<count<<"/"<<time(NULL);
  start_point_x_ = 0;start_point_y_ = 0;
  accumulated_delta_x_ = 0;accumulated_delta_y_ = 0;
  return scroll_result.did_scroll ? DID_HANDLE : DROP_EVENT;


}

/*
InputHandlerProxy::EventDisposition InputHandlerProxy::HandleGestureScrollEnd(
  const WebGestureEvent& gesture_event) {
count = 0;
#ifndef NDEBUG
  DCHECK(expect_scroll_update_end_);
  expect_scroll_update_end_ = false;
#endif
	LOG(INFO)<<"ScrollBegin---------HandleGestureScrollEnd"<<time(NULL);
  if (ShouldAnimate(gesture_event.data.scrollEnd.deltaUnits !=
                    blink::WebGestureEvent::ScrollUnits::Pixels)) {
    // Do nothing if the scroll is being animated; the scroll animation will
    // generate the ScrollEnd when it is done.
  } else {
    cc::ScrollState scroll_state = CreateScrollStateForGesture(gesture_event);
    input_handler_->ScrollEnd(&scroll_state);
  }
  if (!gesture_scroll_on_impl_thread_)
    return DID_NOT_HANDLE;

  if (scroll_elasticity_controller_)
    HandleScrollElasticityOverscroll(gesture_event,
                                     cc::InputHandlerScrollResult());

  gesture_scroll_on_impl_thread_ = false;
  return DID_HANDLE;
}

*/


InputHandlerProxy::EventDisposition InputHandlerProxy::HandleGestureScrollEnd(
  const WebGestureEvent& gesture_event) {
  //LOG(INFO)<<"HandleGestureScrollEndCount+"<<count<<"T"<<time(NULL);
  count = 0;
  last_fps = 0;
#ifndef NDEBUG
  DCHECK(expect_scroll_update_end_);
  expect_scroll_update_end_ = false;
#endif
  //LOG(INFO)<<"ScrollEnd-------------HandleGestureScrollEnd";
  if (ShouldAnimate(gesture_event.data.scrollEnd.deltaUnits !=
                    blink::WebGestureEvent::ScrollUnits::Pixels)) {
    // Do nothing if the scroll is being animated; the scroll animation will
    // generate the ScrollEnd when it is done.
    //LOG(INFO)<<"ScrollEnd-------------ShouldAnimate";
  } else {
    cc::ScrollState scroll_state = CreateScrollStateForGesture(gesture_event);
    //----modify start
    /*if(accumulated_delta_x_ != 0 || accumulated_delta_y_ != 0){
	    cc::ScrollStateData scroll_state_data;
	    scroll_state_data.position_x = start_point_x_;
	    scroll_state_data.position_y = start_point_y_;
	    scroll_state_data.is_beginning = true;
	    scroll_state_data.delta_x = -accumulated_delta_x_;
	    scroll_state_data.delta_y = -accumulated_delta_y_;
	    cc::ScrollState scroll_state_ = cc::ScrollState(scroll_state_data);
	    gfx::Point scroll_point(start_point_x_, start_point_y_);
	    //scroll_state.set_is_ending(false);
	   cc::InputHandlerScrollResult scroll_result =
	      input_handler_->ScrollBy(&scroll_state_);
	  HandleOverscroll(scroll_point, scroll_result, true);
  	  LOG(INFO)<<"ScrollUpdate-------------调用";
	  if (scroll_elasticity_controller_){
	    LOG(INFO)<<"ScrollEnd-------------HandleScrollElasticityOverscroll";
	    HandleScrollElasticityOverscroll(gesture_event, scroll_result);
	  }

    //return scroll_result.did_scroll ? DID_HANDLE : DROP_EVENT;
    LOG(INFO)<<"start_point_x:"<<start_point_x<<"start_point_y:"<<start_point_y;
    LOG(INFO)<<"accumulated_delta_x:"<<accumulated_delta_x<<"accumulated_delta_y:"<<accumulated_delta_y;
    LOG(INFO)<<"ScrollEnd-------------gesture_event.x:"<<gesture_event.x<<"gesture_event.y:"<<gesture_event.x;
    LOG(INFO)<<"ScrollEnd-------------scroll_delta--y"<<gesture_event.data.scrollUpdate.deltaY;
    
   }*/
    start_point_x_ = 0;
    start_point_y_ = 0;
    accumulated_delta_x_ = 0;
    accumulated_delta_y_ = 0;
    //----modify end
    scroll_state.set_is_ending(true);
    input_handler_->ScrollEnd(&scroll_state);
  }
  if (!gesture_scroll_on_impl_thread_)
    return DID_NOT_HANDLE;

  if (scroll_elasticity_controller_)
    HandleScrollElasticityOverscroll(gesture_event, cc::InputHandlerScrollResult());

  gesture_scroll_on_impl_thread_ = false;
  return DID_HANDLE;
}

InputHandlerProxy::EventDisposition InputHandlerProxy::HandleGestureFlingStart(
    const WebGestureEvent& gesture_event) {

  LOG(INFO)<<"-------------HandleGestureFlingStart";
  cc::ScrollState scroll_state = CreateScrollStateForGesture(gesture_event);
  cc::InputHandler::ScrollStatus scroll_status;
  scroll_status.main_thread_scrolling_reasons =
      cc::MainThreadScrollingReason::kNotScrollingOnMain;
  switch (gesture_event.sourceDevice) {
  case blink::WebGestureDeviceTouchpad:
    if (gesture_event.data.flingStart.targetViewport) {
      scroll_status = input_handler_->RootScrollBegin(
          &scroll_state, cc::InputHandler::NON_BUBBLING_GESTURE);
    } else {
      scroll_status = input_handler_->ScrollBegin(
          &scroll_state, cc::InputHandler::NON_BUBBLING_GESTURE);
    }
    break;
  case blink::WebGestureDeviceTouchscreen:
    if (!gesture_scroll_on_impl_thread_) {
      scroll_status.thread = cc::InputHandler::SCROLL_ON_MAIN_THREAD;
      scroll_status.main_thread_scrolling_reasons =
          cc::MainThreadScrollingReason::kContinuingMainThreadScroll;
    } else {
      scroll_status = input_handler_->FlingScrollBegin();//FlingScrollBegin
    }
    break;
  case blink::WebGestureDeviceUninitialized:
    NOTREACHED();
    return DID_NOT_HANDLE;
  }

#ifndef NDEBUG
  expect_scroll_update_end_ = false;
#endif

  switch (scroll_status.thread) {
    case cc::InputHandler::SCROLL_ON_IMPL_THREAD: {
      if (gesture_event.sourceDevice == blink::WebGestureDeviceTouchpad) {
        scroll_state.set_is_ending(true);
        input_handler_->ScrollEnd(&scroll_state);
      }

      const float vx = gesture_event.data.flingStart.velocityX;
      const float vy = gesture_event.data.flingStart.velocityY;
      current_fling_velocity_ = gfx::Vector2dF(vx, vy);
      LOG(INFO)<<"Fling速度vx:"<<vx<<"/vy:"<<vy;

      DCHECK(!current_fling_velocity_.IsZero());
      fling_curve_.reset(client_->CreateFlingAnimationCurve(
          gesture_event.sourceDevice,
          WebFloatPoint(vx, vy),
          blink::WebSize()));
      disallow_horizontal_fling_scroll_ = !vx;
      disallow_vertical_fling_scroll_ = !vy;
      TRACE_EVENT_ASYNC_BEGIN2("input,benchmark,rail",
                               "InputHandlerProxy::HandleGestureFling::started",
                               this, "vx", vx, "vy", vy);
      // Note that the timestamp will only be used to kickstart the animation if
      // its sufficiently close to the timestamp of the first call |Animate()|.
      has_fling_animation_started_ = false;
      fling_parameters_.startTime = gesture_event.timeStampSeconds;
      fling_parameters_.delta = WebFloatPoint(vx, vy);
      fling_parameters_.point = WebPoint(gesture_event.x, gesture_event.y);
      fling_parameters_.globalPoint =
          WebPoint(gesture_event.globalX, gesture_event.globalY);
      fling_parameters_.modifiers = gesture_event.modifiers;
      fling_parameters_.sourceDevice = gesture_event.sourceDevice;
      RequestAnimation();
      return DID_HANDLE;
    }
    case cc::InputHandler::SCROLL_UNKNOWN:
    case cc::InputHandler::SCROLL_ON_MAIN_THREAD: {
      TRACE_EVENT_INSTANT0("input,rail",
                           "InputHandlerProxy::HandleGestureFling::"
                           "scroll_on_main_thread",
                           TRACE_EVENT_SCOPE_THREAD);
      gesture_scroll_on_impl_thread_ = false;
      fling_may_be_active_on_main_thread_ = true;
      client_->DidStartFlinging();
      return DID_NOT_HANDLE;
    }
    case cc::InputHandler::SCROLL_IGNORED: {
      TRACE_EVENT_INSTANT0(
          "input,rail",
          "InputHandlerProxy::HandleGestureFling::ignored",
          TRACE_EVENT_SCOPE_THREAD);
      gesture_scroll_on_impl_thread_ = false;
      if (gesture_event.sourceDevice == blink::WebGestureDeviceTouchpad) {
        // We still pass the curve to the main thread if there's nothing
        // scrollable, in case something
        // registers a handler before the curve is over.
        return DID_NOT_HANDLE;
      }
      return DROP_EVENT;
    }
  }
  return DID_NOT_HANDLE;
}

InputHandlerProxy::EventDisposition InputHandlerProxy::HandleTouchStart(
    const blink::WebTouchEvent& touch_event) {
  //LOG(INFO)<<"-------------HandleTouchStart";
  EventDisposition result = DROP_EVENT;
  for (size_t i = 0; i < touch_event.touchesLength; ++i) {
    if (touch_event.touches[i].state != WebTouchPoint::StatePressed)
      continue;
    if (input_handler_->DoTouchEventsBlockScrollAt(
            gfx::Point(touch_event.touches[i].position.x,
                       touch_event.touches[i].position.y))) {
      result = DID_NOT_HANDLE;
      break;
    }
  }

  // If |result| is DROP_EVENT it wasn't processed above.
  if (result == DROP_EVENT) {
    switch (input_handler_->GetEventListenerProperties(
        cc::EventListenerClass::kTouchStartOrMove)) {
      case cc::EventListenerProperties::kPassive:
        result = DID_HANDLE_NON_BLOCKING;
        break;
      case cc::EventListenerProperties::kBlocking:
        // The touch area rects above already have checked whether it hits
        // a blocking region. Since it does not the event can be dropped.
        result = DROP_EVENT;
        break;
      case cc::EventListenerProperties::kBlockingAndPassive:
        // There is at least one passive listener that needs to possibly
        // be notified so it can't be dropped.
        result = DID_HANDLE_NON_BLOCKING;
        break;
      case cc::EventListenerProperties::kNone:
        result = DROP_EVENT;
        break;
      default:
        NOTREACHED();
        result = DROP_EVENT;
        break;
    }
  }

  // Merge |touch_start_result_| and |result| so the result has the highest
  // priority value according to the sequence; (DROP_EVENT,
  // DID_HANDLE_NON_BLOCKING, DID_NOT_HANDLE).
  if (touch_start_result_ == kEventDispositionUndefined ||
      touch_start_result_ == DROP_EVENT || result == DID_NOT_HANDLE)
    touch_start_result_ = result;

  // If |result| is still DROP_EVENT look at the touch end handler as
  // we may not want to discard the entire touch sequence. Note this
  // code is explicitly after the assignment of the |touch_start_result_|
  // so the touch moves are not sent to the main thread un-necessarily.
  if (result == DROP_EVENT &&
      input_handler_->GetEventListenerProperties(
          cc::EventListenerClass::kTouchEndOrCancel) !=
          cc::EventListenerProperties::kNone) {
    result = DID_HANDLE_NON_BLOCKING;
  }

  return result;
}

InputHandlerProxy::EventDisposition InputHandlerProxy::HandleTouchMove(
    const blink::WebTouchEvent& touch_event) {
  if (touch_start_result_ != kEventDispositionUndefined)
    return static_cast<EventDisposition>(touch_start_result_);
  return DID_NOT_HANDLE;
}

InputHandlerProxy::EventDisposition InputHandlerProxy::HandleTouchEnd(
    const blink::WebTouchEvent& touch_event) {
  //LOG(INFO)<<"-------------HandleTouchEnd";
  if (touch_event.touchesLength == 1)
    touch_start_result_ = kEventDispositionUndefined;
  return DID_NOT_HANDLE;
}

bool InputHandlerProxy::FilterInputEventForFlingBoosting(
    const WebInputEvent& event) {
  
  //LOG(INFO)<<"-------------FilterInputEventForFlingBoosting";
  return false;
  if (!WebInputEvent::isGestureEventType(event.type))
    return false;

  if (!fling_curve_) {
    DCHECK(!deferred_fling_cancel_time_seconds_);
    return false;
  }

  const WebGestureEvent& gesture_event =
      static_cast<const WebGestureEvent&>(event);
  if (gesture_event.type == WebInputEvent::GestureFlingCancel) {
    if (gesture_event.data.flingCancel.preventBoosting)
      return false;

    if (current_fling_velocity_.LengthSquared() < kMinBoostFlingSpeedSquare)
      return false;

    TRACE_EVENT_INSTANT0("input",
                         "InputHandlerProxy::FlingBoostStart",
                         TRACE_EVENT_SCOPE_THREAD);
    deferred_fling_cancel_time_seconds_ =
        event.timeStampSeconds + kFlingBoostTimeoutDelaySeconds;
    return true;
  }

  // A fling is either inactive or is "free spinning", i.e., has yet to be
  // interrupted by a touch gesture, in which case there is nothing to filter.
  if (!deferred_fling_cancel_time_seconds_)
    return false;

  // Gestures from a different source should immediately interrupt the fling.
  if (gesture_event.sourceDevice != fling_parameters_.sourceDevice) {
    CancelCurrentFling();
    return false;
  }

  switch (gesture_event.type) {
    case WebInputEvent::GestureTapCancel:
    case WebInputEvent::GestureTapDown:
      return false;

    case WebInputEvent::GestureScrollBegin:
      //LOG(INFO)<<"-------------FilterInputEventForFlingBoosting----GestureScrollBegin";
      if (!input_handler_->IsCurrentlyScrollingLayerAt(
              gfx::Point(gesture_event.x, gesture_event.y),
              fling_parameters_.sourceDevice == blink::WebGestureDeviceTouchpad
                  ? cc::InputHandler::NON_BUBBLING_GESTURE
                  : cc::InputHandler::TOUCHSCREEN)) {
        CancelCurrentFling();
        return false;
      }

      // TODO(jdduke): Use |gesture_event.data.scrollBegin.delta{X,Y}Hint| to
      // determine if the ScrollBegin should immediately cancel the fling.
      ExtendBoostedFlingTimeout(gesture_event);
      return true;

    case WebInputEvent::GestureScrollUpdate: {
      LOG(INFO)<<"-------------FilterInputEventForFlingBoosting----GestureScrollUpdate";
      const double time_since_last_boost_event =
          event.timeStampSeconds - last_fling_boost_event_.timeStampSeconds;
      const double time_since_last_fling_animate = std::max(
          0.0, event.timeStampSeconds - InSecondsF(last_fling_animate_time_));
      if (ShouldSuppressScrollForFlingBoosting(current_fling_velocity_,
                                               gesture_event,
                                               time_since_last_boost_event,
                                               time_since_last_fling_animate)) {
        LOG(INFO)<<"-------------FilterInputEventForFlingBoosting----ShouldSuppressScrollForFlingBoosting";
        ExtendBoostedFlingTimeout(gesture_event);
        return true;
      }

      CancelCurrentFling();
      return false;
    }

    case WebInputEvent::GestureScrollEnd:
      // Clear the last fling boost event *prior* to fling cancellation,
      // preventing insertion of a synthetic GestureScrollBegin.
      LOG(INFO)<<"-------------FilterInputEventForFlingBoosting----GestureScrollEnd";
      last_fling_boost_event_ = WebGestureEvent();
      CancelCurrentFling();
      return true;

    case WebInputEvent::GestureFlingStart: {
      DCHECK_EQ(fling_parameters_.sourceDevice, gesture_event.sourceDevice);
      LOG(INFO)<<"-------------FilterInputEventForFlingBoosting----GestureFlingStart";
      bool fling_boosted =
          fling_parameters_.modifiers == gesture_event.modifiers &&
          ShouldBoostFling(current_fling_velocity_, gesture_event);

      gfx::Vector2dF new_fling_velocity(
          gesture_event.data.flingStart.velocityX,
          gesture_event.data.flingStart.velocityY);
      DCHECK(!new_fling_velocity.IsZero());

      if (fling_boosted)
        current_fling_velocity_ += new_fling_velocity;
      else
        current_fling_velocity_ = new_fling_velocity;

      WebFloatPoint velocity(current_fling_velocity_.x(),
                             current_fling_velocity_.y());
      deferred_fling_cancel_time_seconds_ = 0;
      disallow_horizontal_fling_scroll_ = !velocity.x;
      disallow_vertical_fling_scroll_ = !velocity.y;
      last_fling_boost_event_ = WebGestureEvent();
      fling_curve_.reset(client_->CreateFlingAnimationCurve(
          gesture_event.sourceDevice,
          velocity,
          blink::WebSize()));
      fling_parameters_.startTime = gesture_event.timeStampSeconds;
      fling_parameters_.delta = velocity;
      fling_parameters_.point = WebPoint(gesture_event.x, gesture_event.y);
      fling_parameters_.globalPoint =
          WebPoint(gesture_event.globalX, gesture_event.globalY);

      TRACE_EVENT_INSTANT2("input",
                           fling_boosted ? "InputHandlerProxy::FlingBoosted"
                                         : "InputHandlerProxy::FlingReplaced",
                           TRACE_EVENT_SCOPE_THREAD,
                           "vx",
                           current_fling_velocity_.x(),
                           "vy",
                           current_fling_velocity_.y());

      // The client expects balanced calls between a consumed GestureFlingStart
      // and |DidStopFlinging()|.
      client_->DidStopFlinging();
      return true;
    }

    default:
      //LOG(INFO)<<"-------------FilterInputEventForFlingBoosting----default";
      // All other types of gestures (taps, presses, etc...) will complete the
      // deferred fling cancellation.
      CancelCurrentFling();
      return false;
  }
}

void InputHandlerProxy::ExtendBoostedFlingTimeout(
    const blink::WebGestureEvent& event) {
  TRACE_EVENT_INSTANT0("input",
                       "InputHandlerProxy::ExtendBoostedFlingTimeout",
                       TRACE_EVENT_SCOPE_THREAD);
  deferred_fling_cancel_time_seconds_ =
      event.timeStampSeconds + kFlingBoostTimeoutDelaySeconds;
  last_fling_boost_event_ = event;
}

void InputHandlerProxy::Animate(base::TimeTicks time) {
  // If using synchronous animate, then only expect Animate attempts started by
  // the synchronous system. Don't let the InputHandler try to Animate also.
  DCHECK(!input_handler_->IsCurrentlyScrollingViewport() ||
         allow_root_animate_);

  if (scroll_elasticity_controller_)
    scroll_elasticity_controller_->Animate(time);

  if (!fling_curve_)
    return;

  last_fling_animate_time_ = time;
  double monotonic_time_sec = InSecondsF(time);

  if (deferred_fling_cancel_time_seconds_ &&
      monotonic_time_sec > deferred_fling_cancel_time_seconds_) {
    CancelCurrentFling();
    return;
  }

  client_->DidAnimateForInput();

  if (!has_fling_animation_started_) {
    has_fling_animation_started_ = true;
    // Guard against invalid, future or sufficiently stale start times, as there
    // are no guarantees fling event and animation timestamps are compatible.
    if (!fling_parameters_.startTime ||
        monotonic_time_sec <= fling_parameters_.startTime ||
        monotonic_time_sec >= fling_parameters_.startTime +
                                  kMaxSecondsFromFlingTimestampToFirstAnimate) {
      fling_parameters_.startTime = monotonic_time_sec;
      RequestAnimation();
      return;
    }
  }

  bool fling_is_active =
      fling_curve_->apply(monotonic_time_sec - fling_parameters_.startTime,
                          this);

  if (disallow_vertical_fling_scroll_ && disallow_horizontal_fling_scroll_)
    fling_is_active = false;

  if (fling_is_active) {
    RequestAnimation();
  } else {
    TRACE_EVENT_INSTANT0("input",
                         "InputHandlerProxy::animate::flingOver",
                         TRACE_EVENT_SCOPE_THREAD);
    CancelCurrentFling();
  }
}

void InputHandlerProxy::MainThreadHasStoppedFlinging() {
  fling_may_be_active_on_main_thread_ = false;
  client_->DidStopFlinging();
}

void InputHandlerProxy::ReconcileElasticOverscrollAndRootScroll() {
  if (scroll_elasticity_controller_)
    scroll_elasticity_controller_->ReconcileStretchAndScroll();
}

void InputHandlerProxy::UpdateRootLayerStateForSynchronousInputHandler(
    const gfx::ScrollOffset& total_scroll_offset,
    const gfx::ScrollOffset& max_scroll_offset,
    const gfx::SizeF& scrollable_size,
    float page_scale_factor,
    float min_page_scale_factor,
    float max_page_scale_factor) {
  if (synchronous_input_handler_) {
    synchronous_input_handler_->UpdateRootLayerState(
        total_scroll_offset, max_scroll_offset, scrollable_size,
        page_scale_factor, min_page_scale_factor, max_page_scale_factor);
  }
}

void InputHandlerProxy::SetOnlySynchronouslyAnimateRootFlings(
    SynchronousInputHandler* synchronous_input_handler) {
  allow_root_animate_ = !synchronous_input_handler;
  synchronous_input_handler_ = synchronous_input_handler;
  if (synchronous_input_handler_)
    input_handler_->RequestUpdateForSynchronousInputHandler();
}

void InputHandlerProxy::SynchronouslyAnimate(base::TimeTicks time) {
  // When this function is used, SetOnlySynchronouslyAnimate() should have been
  // previously called. IOW you should either be entirely in synchronous mode or
  // not.
  DCHECK(synchronous_input_handler_);
  DCHECK(!allow_root_animate_);
  base::AutoReset<bool> reset(&allow_root_animate_, true);
  Animate(time);
}

void InputHandlerProxy::SynchronouslySetRootScrollOffset(
    const gfx::ScrollOffset& root_offset) {
  DCHECK(synchronous_input_handler_);
  input_handler_->SetSynchronousInputHandlerRootScrollOffset(root_offset);
}

void InputHandlerProxy::SynchronouslyZoomBy(float magnify_delta,
                                            const gfx::Point& anchor) {
  DCHECK(synchronous_input_handler_);
  input_handler_->PinchGestureBegin();
  input_handler_->PinchGestureUpdate(magnify_delta, anchor);
  input_handler_->PinchGestureEnd();
}

void InputHandlerProxy::HandleOverscroll(
    const gfx::Point& causal_event_viewport_point,
    const cc::InputHandlerScrollResult& scroll_result,
    bool bundle_overscroll_params_with_ack) {
  DCHECK(client_);

  //LOG(INFO)<<"-------------HandleOverscroll";
  if (!scroll_result.did_overscroll_root)
    return;

  TRACE_EVENT2("input",
               "InputHandlerProxy::DidOverscroll",
               "dx",
               scroll_result.unused_scroll_delta.x(),
               "dy",
               scroll_result.unused_scroll_delta.y());

  if (fling_curve_) {
    static const int kFlingOverscrollThreshold = 1;
    disallow_horizontal_fling_scroll_ |=
        std::abs(scroll_result.accumulated_root_overscroll.x()) >=
        kFlingOverscrollThreshold;
    disallow_vertical_fling_scroll_ |=
        std::abs(scroll_result.accumulated_root_overscroll.y()) >=
        kFlingOverscrollThreshold;
  }

  if (bundle_overscroll_params_with_ack) {
    // Bundle overscroll message with triggering event response, saving an IPC.
    current_overscroll_params_.reset(new DidOverscrollParams());
    current_overscroll_params_->accumulated_overscroll =
        scroll_result.accumulated_root_overscroll;
    current_overscroll_params_->latest_overscroll_delta =
        scroll_result.unused_scroll_delta;
    current_overscroll_params_->current_fling_velocity =
        ToClientScrollIncrement(current_fling_velocity_);
    current_overscroll_params_->causal_event_viewport_point =
        gfx::PointF(causal_event_viewport_point);
    return;
  }

  client_->DidOverscroll(scroll_result.accumulated_root_overscroll,
                         scroll_result.unused_scroll_delta,
                         ToClientScrollIncrement(current_fling_velocity_),
                         gfx::PointF(causal_event_viewport_point));
}

bool InputHandlerProxy::CancelCurrentFling() {
  //LOG(INFO)<<"InputHandlerProxy::CancelCurrentFling";
  if (CancelCurrentFlingWithoutNotifyingClient()) {
    client_->DidStopFlinging();
    return true;
  }
  return false;
}

bool InputHandlerProxy::CancelCurrentFlingWithoutNotifyingClient() {
  bool had_fling_animation = !!fling_curve_;
  if (had_fling_animation &&
      fling_parameters_.sourceDevice == blink::WebGestureDeviceTouchscreen) {
    cc::ScrollStateData scroll_state_data;
    scroll_state_data.is_ending = true;
    cc::ScrollState scroll_state(scroll_state_data);
    input_handler_->ScrollEnd(&scroll_state);
    TRACE_EVENT_ASYNC_END0(
        "input",
        "InputHandlerProxy::HandleGestureFling::started",
        this);
  }

  TRACE_EVENT_INSTANT1("input",
                       "InputHandlerProxy::CancelCurrentFling",
                       TRACE_EVENT_SCOPE_THREAD,
                       "had_fling_animation",
                       had_fling_animation);
  fling_curve_.reset();
  has_fling_animation_started_ = false;
  gesture_scroll_on_impl_thread_ = false;
  current_fling_velocity_ = gfx::Vector2dF();
  fling_parameters_ = blink::WebActiveWheelFlingParameters();

  if (deferred_fling_cancel_time_seconds_) {
    deferred_fling_cancel_time_seconds_ = 0;

    WebGestureEvent last_fling_boost_event = last_fling_boost_event_;
    last_fling_boost_event_ = WebGestureEvent();
    if (last_fling_boost_event.type == WebInputEvent::GestureScrollBegin ||
        last_fling_boost_event.type == WebInputEvent::GestureScrollUpdate) {
      // Synthesize a GestureScrollBegin, as the original was suppressed.
      HandleInputEvent(ObtainGestureScrollBegin(last_fling_boost_event));
    }
  }

  return had_fling_animation;
}

void InputHandlerProxy::RequestAnimation() {
  // When a SynchronousInputHandler is present, root flings should go through
  // it to allow it to control when or if the root fling is animated. Non-root
  // flings always go through the normal InputHandler.
  if (synchronous_input_handler_ &&
      input_handler_->IsCurrentlyScrollingViewport())
    synchronous_input_handler_->SetNeedsSynchronousAnimateInput();
  else
    input_handler_->SetNeedsAnimateInput();
}

bool InputHandlerProxy::TouchpadFlingScroll(
    const WebFloatSize& increment) {
  InputHandlerProxy::EventDisposition disposition;
  cc::EventListenerProperties properties =
      input_handler_->GetEventListenerProperties(
          cc::EventListenerClass::kMouseWheel);
  switch (properties) {
    case cc::EventListenerProperties::kBlocking:
      disposition = DID_NOT_HANDLE;
      break;
    case cc::EventListenerProperties::kPassive:
    case cc::EventListenerProperties::kNone: {
      WebMouseWheelEvent synthetic_wheel;
      synthetic_wheel.type = WebInputEvent::MouseWheel;
      synthetic_wheel.timeStampSeconds = InSecondsF(base::TimeTicks::Now());
      synthetic_wheel.deltaX = increment.width;
      synthetic_wheel.deltaY = increment.height;
      synthetic_wheel.hasPreciseScrollingDeltas = true;
      synthetic_wheel.x = fling_parameters_.point.x;
      synthetic_wheel.y = fling_parameters_.point.y;
      synthetic_wheel.globalX = fling_parameters_.globalPoint.x;
      synthetic_wheel.globalY = fling_parameters_.globalPoint.y;
      synthetic_wheel.modifiers = fling_parameters_.modifiers;

      disposition = ScrollByMouseWheel(synthetic_wheel, properties);

      // Send the event over to the main thread.
      if (disposition == DID_HANDLE_NON_BLOCKING) {
        client_->DispatchNonBlockingEventToMainThread(
            ui::WebInputEventTraits::Clone(synthetic_wheel), ui::LatencyInfo());
      }
      break;
    }
    default:
      NOTREACHED();
      return false;
  }

  switch (disposition) {
    case DID_HANDLE:
    case DID_HANDLE_NON_BLOCKING:
      return true;
    case DROP_EVENT:
      break;
    case DID_NOT_HANDLE:
      TRACE_EVENT_INSTANT0("input",
                           "InputHandlerProxy::scrollBy::AbortFling",
                           TRACE_EVENT_SCOPE_THREAD);
      // If we got a DID_NOT_HANDLE, that means we need to deliver wheels on the
      // main thread. In this case we need to schedule a commit and transfer the
      // fling curve over to the main thread and run the rest of the wheels from
      // there. This can happen when flinging a page that contains a scrollable
      // subarea that we can't scroll on the thread if the fling starts outside
      // the subarea but then is flung "under" the pointer.
      client_->TransferActiveWheelFlingAnimation(fling_parameters_);
      fling_may_be_active_on_main_thread_ = true;
      client_->DidStartFlinging();
      CancelCurrentFlingWithoutNotifyingClient();
      break;
  }

  return false;
}



bool InputHandlerProxy::scrollBy(const WebFloatSize& increment,
                                 const WebFloatSize& velocity) {
  //LOG(INFO)<<"------------InputHandlerProxy::scrollBy"<<time(NULL);
  WebFloatSize clipped_increment;
  WebFloatSize clipped_velocity;
  if (!disallow_horizontal_fling_scroll_) {
    clipped_increment.width = increment.width;
    clipped_velocity.width = velocity.width;
  }
  if (!disallow_vertical_fling_scroll_) {
    clipped_increment.height = increment.height;
    clipped_velocity.height = velocity.height;
  }

  current_fling_velocity_ = clipped_velocity;
  LOG(INFO)<<"current_fling_velocity_.height:"<<clipped_velocity.height;
  // Early out if the increment is zero, but avoid early termination if the
  // velocity is still non-zero.
  if (clipped_increment == WebFloatSize())
    return clipped_velocity != WebFloatSize();

  TRACE_EVENT2("input",
               "InputHandlerProxy::scrollBy",
               "x",
               clipped_increment.width,
               "y",
               clipped_increment.height);

  bool did_scroll = false;

  switch (fling_parameters_.sourceDevice) {
    case blink::WebGestureDeviceTouchpad:
      did_scroll = TouchpadFlingScroll(clipped_increment);
      break;
    case blink::WebGestureDeviceTouchscreen: {
      clipped_increment = ToClientScrollIncrement(clipped_increment);
      cc::ScrollStateData scroll_state_data;
      scroll_state_data.delta_x = clipped_increment.width;
      scroll_state_data.delta_y = clipped_increment.height;
      scroll_state_data.velocity_x = clipped_velocity.width;
      scroll_state_data.velocity_y = clipped_velocity.height;
      scroll_state_data.is_in_inertial_phase = true;
      cc::ScrollState scroll_state(scroll_state_data);
      cc::InputHandlerScrollResult scroll_result =
          input_handler_->ScrollBy(&scroll_state);
      HandleOverscroll(fling_parameters_.point, scroll_result, false);
      did_scroll = scroll_result.did_scroll;
    } break;
    case blink::WebGestureDeviceUninitialized:
      NOTREACHED();
      return false;
  }

  if (did_scroll) {
    fling_parameters_.cumulativeScroll.width += clipped_increment.width;
    fling_parameters_.cumulativeScroll.height += clipped_increment.height;
  }

  // It's possible the provided |increment| is sufficiently small as to not
  // trigger a scroll, e.g., with a trivial time delta between fling updates.
  // Return true in this case to prevent early fling termination.
  if (std::abs(clipped_increment.width) < kScrollEpsilon &&
      std::abs(clipped_increment.height) < kScrollEpsilon)
    return true;

  return did_scroll;
}

void InputHandlerProxy::HandleScrollElasticityOverscroll(
    const WebGestureEvent& gesture_event,
    const cc::InputHandlerScrollResult& scroll_result) {
  DCHECK(scroll_elasticity_controller_);
  LOG(INFO)<<"------------InputHandlerProxy::HandleScrollElasticityOverscroll";
  // Send the event and its disposition to the elasticity controller to update
  // the over-scroll animation. Note that the call to the elasticity controller
  // is made asynchronously, to minimize divergence between main thread and
  // impl thread event handling paths.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(&InputScrollElasticityController::ObserveGestureEventAndResult,
                 scroll_elasticity_controller_->GetWeakPtr(), gesture_event,
                 scroll_result));
}

}  // namespace ui