// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/memory/memory_coordinator_impl.h"

#include "base/run_loop.h"
#include "content/browser/memory/memory_monitor.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

namespace {

class MockMemoryCoordinatorClient : public base::MemoryCoordinatorClient {
 public:
  void OnMemoryStateChange(base::MemoryState state) override {
    is_called_ = true;
    state_ = state;
  }

  bool is_called() { return is_called_; }
  base::MemoryState state() { return state_; }

 private:
  bool is_called_ = false;
  base::MemoryState state_ = base::MemoryState::NORMAL;
};

}  // namespace

class MockMemoryMonitor : public MemoryMonitor {
 public:
  MockMemoryMonitor() {}
  ~MockMemoryMonitor() override {}

  void SetFreeMemoryUntilCriticalMB(int free_memory) {
    free_memory_ = free_memory;
  }

  // MemoryMonitor implementation
  int GetFreeMemoryUntilCriticalMB() override { return free_memory_; }

 private:
  int free_memory_ = 0;

  DISALLOW_COPY_AND_ASSIGN(MockMemoryMonitor);
};

class MemoryCoordinatorImplTest : public testing::Test {
 public:
  void SetUp() override {
    coordinator_.reset(new MemoryCoordinatorImpl(
        message_loop_.task_runner(), base::WrapUnique(new MockMemoryMonitor)));
  }

  MockMemoryMonitor* GetMockMemoryMonitor() {
    return static_cast<MockMemoryMonitor*>(coordinator_->memory_monitor());
  }

 protected:
  std::unique_ptr<MemoryCoordinatorImpl> coordinator_;
  base::MessageLoop message_loop_;
};

TEST_F(MemoryCoordinatorImplTest, CalculateNextState) {
  coordinator_->expected_renderer_size_ = 10;
  coordinator_->new_renderers_until_throttled_ = 4;
  coordinator_->new_renderers_until_suspended_ = 2;
  coordinator_->new_renderers_back_to_normal_ = 5;
  coordinator_->new_renderers_back_to_throttled_ = 3;
  DCHECK(coordinator_->ValidateParameters());

  // The default state is NORMAL.
  EXPECT_EQ(base::MemoryState::NORMAL, coordinator_->GetCurrentMemoryState());

  // Transitions from NORMAL
  coordinator_->current_state_ = base::MemoryState::NORMAL;
  EXPECT_EQ(base::MemoryState::NORMAL, coordinator_->GetCurrentMemoryState());
  GetMockMemoryMonitor()->SetFreeMemoryUntilCriticalMB(50);
  EXPECT_EQ(base::MemoryState::NORMAL, coordinator_->CalculateNextState());
  GetMockMemoryMonitor()->SetFreeMemoryUntilCriticalMB(40);
  EXPECT_EQ(base::MemoryState::THROTTLED, coordinator_->CalculateNextState());
  GetMockMemoryMonitor()->SetFreeMemoryUntilCriticalMB(20);
  EXPECT_EQ(base::MemoryState::SUSPENDED, coordinator_->CalculateNextState());

  // Transitions from THROTTLED
  coordinator_->current_state_ = base::MemoryState::THROTTLED;
  EXPECT_EQ(base::MemoryState::THROTTLED,
            coordinator_->GetCurrentMemoryState());
  GetMockMemoryMonitor()->SetFreeMemoryUntilCriticalMB(40);
  EXPECT_EQ(base::MemoryState::THROTTLED, coordinator_->CalculateNextState());
  GetMockMemoryMonitor()->SetFreeMemoryUntilCriticalMB(50);
  EXPECT_EQ(base::MemoryState::NORMAL, coordinator_->CalculateNextState());
  GetMockMemoryMonitor()->SetFreeMemoryUntilCriticalMB(20);
  EXPECT_EQ(base::MemoryState::SUSPENDED, coordinator_->CalculateNextState());

  // Transitions from SUSPENDED
  coordinator_->current_state_ = base::MemoryState::SUSPENDED;
  EXPECT_EQ(base::MemoryState::SUSPENDED,
            coordinator_->GetCurrentMemoryState());
  GetMockMemoryMonitor()->SetFreeMemoryUntilCriticalMB(20);
  EXPECT_EQ(base::MemoryState::SUSPENDED, coordinator_->CalculateNextState());
  GetMockMemoryMonitor()->SetFreeMemoryUntilCriticalMB(30);
  EXPECT_EQ(base::MemoryState::THROTTLED, coordinator_->CalculateNextState());
  GetMockMemoryMonitor()->SetFreeMemoryUntilCriticalMB(50);
  EXPECT_EQ(base::MemoryState::NORMAL, coordinator_->CalculateNextState());
}

TEST_F(MemoryCoordinatorImplTest, UpdateState) {
  coordinator_->expected_renderer_size_ = 10;
  coordinator_->new_renderers_until_throttled_ = 4;
  coordinator_->new_renderers_until_suspended_ = 2;
  coordinator_->new_renderers_back_to_normal_ = 5;
  coordinator_->new_renderers_back_to_throttled_ = 3;
  DCHECK(coordinator_->ValidateParameters());

  {
    // Transition happens (NORMAL -> THROTTLED).
    MockMemoryCoordinatorClient client;
    base::MemoryCoordinatorClientRegistry::GetInstance()->Register(&client);
    coordinator_->current_state_ = base::MemoryState::NORMAL;
    GetMockMemoryMonitor()->SetFreeMemoryUntilCriticalMB(40);
    coordinator_->UpdateState();
    base::RunLoop loop;
    loop.RunUntilIdle();
    EXPECT_TRUE(client.is_called());
    EXPECT_EQ(base::MemoryState::THROTTLED, client.state());
    base::MemoryCoordinatorClientRegistry::GetInstance()->Unregister(&client);
  }

  {
    // No transtion (NORMAL -> NORMAL). OnStateChange shouldn't be called.
    MockMemoryCoordinatorClient client;
    base::MemoryCoordinatorClientRegistry::GetInstance()->Register(&client);
    coordinator_->current_state_ = base::MemoryState::NORMAL;
    GetMockMemoryMonitor()->SetFreeMemoryUntilCriticalMB(50);
    coordinator_->UpdateState();
    base::RunLoop loop;
    loop.RunUntilIdle();
    EXPECT_FALSE(client.is_called());
    EXPECT_EQ(base::MemoryState::NORMAL, client.state());
    base::MemoryCoordinatorClientRegistry::GetInstance()->Unregister(&client);
  }
}

}  // namespace content
