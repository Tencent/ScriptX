/*
 * Tencent is pleased to support the open source community by making ScriptX available.
 * Copyright (C) 2023 THL A29 Limited, a Tencent company.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "V8Platform.h"
#include <libplatform/libplatform.h>
#include <type_traits>
#include "../../src/Utils.h"
#include "V8Engine.h"
#include "V8Helper.hpp"

namespace script::v8_backend {

class MessageQueueTaskRunner : public v8::TaskRunner {
 private:
  V8Platform* platform_{};
  v8::Isolate* isolate_{};
  std::shared_ptr<v8::TaskRunner> defaultTaskRunner_{};

  V8Engine* engine_{};

  std::atomic_bool isPumpScheduled_{false};

 public:
  MessageQueueTaskRunner() = default;

  SCRIPTX_DISALLOW_COPY_AND_MOVE(MessageQueueTaskRunner);

  ~MessageQueueTaskRunner() override = default;

  bool hasRunner() const { return defaultTaskRunner_ != nullptr; }

  void setQueue(V8Platform* platform, v8::Isolate* isolate,
                std::shared_ptr<v8::TaskRunner> defaultTaskRunner) {
    platform_ = platform;
    isolate_ = isolate;
    defaultTaskRunner_ = std::move(defaultTaskRunner);
  }

  void setEngine(V8Engine* engine) { engine_ = engine; }

  void PostTask(std::unique_ptr<v8::Task> task) override {
    defaultTaskRunner_->PostTask(std::move(task));
    schedulePump();
  }

  void PostDelayedTask(std::unique_ptr<v8::Task> task, double delay_in_seconds) override {
    defaultTaskRunner_->PostDelayedTask(std::move(task), delay_in_seconds);
    schedulePump();
  }

  void PostIdleTask(std::unique_ptr<v8::IdleTask> task) override {
    defaultTaskRunner_->PostIdleTask(std::move(task));
    schedulePump();
  }

  bool IdleTasksEnabled() override { return defaultTaskRunner_->IdleTasksEnabled(); }

  void PostNonNestableTask(std::unique_ptr<v8::Task> task) override { PostTask(std::move(task)); }

  void PostNonNestableDelayedTask(std::unique_ptr<v8::Task> task,
                                  double delay_in_seconds) override {
    PostDelayedTask(std::move(task), delay_in_seconds);
  }

  bool NonNestableTasksEnabled() const override { return true; }

  bool NonNestableDelayedTasksEnabled() const override { return true; }

 private:
  void schedulePump() {
    bool expected = false;
    if (engine_ && isPumpScheduled_.compare_exchange_strong(expected, true)) {
      script::utils::Message s(
          [](auto& msg) {
            static_cast<MessageQueueTaskRunner*>(msg.ptr0)->isPumpScheduled_ = false;
            auto platform = static_cast<V8Platform*>(msg.ptr1);
            auto isolate = static_cast<v8::Isolate*>(msg.ptr2);

            while (platform->pumpMessageQueue(isolate)) {
              // loop until no more message to pump
            }
          },
          nullptr);

      s.name = "SchedulePump";
      s.ptr0 = this;
      s.ptr1 = platform_;
      s.ptr2 = isolate_;
      s.tag = engine_;

      engine_->messageQueue()->postMessage(s);
    }
  }
};

V8Platform::EngineData::EngineData()
    : messageQueueRunner(std::make_shared<MessageQueueTaskRunner>()) {}

// lock_ should be declared before singletonInstance_
// because singletonInstance_ depends on lock_
// so the initialize order should be lock_ -> singletonInstance_
// while the de-initialize order be singletonInstance_ -> lock_
// reference: "Dynamic initialization' rule 3
// https://en.cppreference.com/w/cpp/language/initialization
std::mutex V8Platform::lock_;
std::shared_ptr<V8Platform> V8Platform::singletonInstance_;

std::shared_ptr<V8Platform> V8Platform::getPlatform() {
  std::lock_guard<std::mutex> lock(lock_);
  if (!singletonInstance_) {
    singletonInstance_ = std::shared_ptr<V8Platform>(new V8Platform());
  }
  return singletonInstance_;
}

void V8Platform::addEngineInstance(v8::Isolate* isolate, script::v8_backend::V8Engine* engine) {
  std::lock_guard<std::mutex> lock(lock_);
  engineMap_[isolate].messageQueueRunner->setEngine(engine);
}

void V8Platform::removeEngineInstance(v8::Isolate* isolate) {
  std::lock_guard<std::mutex> lock(lock_);
  auto it = engineMap_.find(isolate);
  if (it != engineMap_.end()) {
    engineMap_.erase(isolate);
  }
}

// the following comment from v8/src/init.cc AdvanceStartupState function
// the calling order are strongly enforced by v8
//
// Ensure the following order:
// v8::V8::InitializePlatform(platform);
// v8::V8::Initialize();
// v8::Isolate* isolate = v8::Isolate::New(...);
// ...
// isolate->Dispose();
// v8::V8::Dispose();
// v8::V8::DisposePlatform();

V8Platform::V8Platform() : defaultPlatform_(v8::platform::NewDefaultPlatform()) {
  // constructor is called inside lock_guard of lock_
  v8::V8::InitializePlatform(this);
  v8::V8::Initialize();
}

V8Platform::~V8Platform() {
  std::lock_guard<std::mutex> lock(lock_);
  v8::V8::Dispose();

#if SCRIPTX_V8_VERSION_AT_LEAST(10, 0)
  v8::V8::DisposePlatform();
#else
  // DEPRECATED in 10.0 36707481ffa
  v8::V8::ShutdownPlatform();
#endif
}

std::shared_ptr<v8::TaskRunner> V8Platform::GetForegroundTaskRunner(v8::Isolate* isolate) {
  std::lock_guard<std::mutex> lock(lock_);
  auto queueRunner = engineMap_[isolate].messageQueueRunner;
  if (!queueRunner->hasRunner()) {
    // this method may be called during creating Isolate...
    // set anything we now ASAP
    queueRunner->setQueue(this, isolate, defaultPlatform_->GetForegroundTaskRunner(isolate));
  }

  return queueRunner;
}

bool V8Platform::pumpMessageQueue(v8::Isolate* isolate) {
  v8::Locker locker(isolate);
  return v8::platform::PumpMessageLoop(defaultPlatform_.get(), isolate);
}

void V8Platform::OnCriticalMemoryPressure() {
  Logger() << "V8Platform::OnCriticalMemoryPressure()";
  return defaultPlatform_->OnCriticalMemoryPressure();
}

#if SCRIPTX_V8_VERSION_AT_MOST(10, 6)
bool V8Platform::OnCriticalMemoryPressure(size_t length) {
  Logger() << "V8Platform::OnCriticalMemoryPressure(" << length << ")";
  return defaultPlatform_->OnCriticalMemoryPressure(length);
}
#endif

}  // namespace script::v8_backend

void script::v8_interop::Critical::neverDestroyPlatform() {
  using script::v8_backend::V8Platform;
  auto platform = V8Platform::getPlatform();

  // make the shared_ptr leak
  std::aligned_storage_t<sizeof(std::shared_ptr<V8Platform>)> buffer;
  new (&buffer) std::shared_ptr<V8Platform>(platform);
}

void script::v8_interop::Critical::immediatelyDestroyPlatform() {
  using script::v8_backend::V8Platform;
  std::lock_guard<std::mutex> lock(V8Platform::lock_);
  V8Platform::singletonInstance_ = nullptr;
}
