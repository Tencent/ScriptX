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
#include <cstdlib>
#include <type_traits>
#include <utility>
#include "../../src/Utils.h"
#include "V8Engine.h"
#include "V8Helper.hpp"

namespace script::v8_backend {

class MessageQueueTaskRunner : public v8::TaskRunner {
 private:
  v8::Isolate* isolate_{};
  V8Engine* engine_{};

 public:
  MessageQueueTaskRunner() = default;

  SCRIPTX_DISALLOW_COPY_AND_MOVE(MessageQueueTaskRunner);

  ~MessageQueueTaskRunner() override = default;

  bool inited() const { return isolate_ != nullptr; }

  void setQueue(v8::Isolate* isolate) { isolate_ = isolate; }

  void setEngine(V8Engine* engine) { engine_ = engine; }

#if SCRIPTX_V8_VERSION_GE(12, 4)  // changed to PostTaskImpl
  void PostTaskImpl(std::unique_ptr<v8::Task> task, const v8::SourceLocation& location) override {
    scheduleTask(std::move(task));
  }

  void PostDelayedTaskImpl(std::unique_ptr<v8::Task> task, double delay_in_seconds,
                           const v8::SourceLocation& location) override {
    scheduleTask(std::move(task), delay_in_seconds);
  }

  void PostIdleTaskImpl(std::unique_ptr<v8::IdleTask> task,
                        const v8::SourceLocation& location) override {
    ::abort();
  }

  void PostNonNestableTaskImpl(std::unique_ptr<v8::Task> task,
                               const v8::SourceLocation& location) override {
    scheduleTask(std::move(task));
  }

  void PostNonNestableDelayedTaskImpl(std::unique_ptr<v8::Task> task, double delay_in_seconds,
                                      const v8::SourceLocation& location) override {
    scheduleTask(std::move(task), delay_in_seconds);
  }

#else
  void PostTask(std::unique_ptr<v8::Task> task) override { scheduleTask(std::move(task)); }

  void PostDelayedTask(std::unique_ptr<v8::Task> task, double delay_in_seconds) override {
    scheduleTask(std::move(task), delay_in_seconds);
  }

  void PostIdleTask(std::unique_ptr<v8::IdleTask> task) override {
    // not supported
    ::abort();
  }

  void PostNonNestableTask(std::unique_ptr<v8::Task> task) override {
    scheduleTask(std::move(task));
  }
#endif

  bool IdleTasksEnabled() override { return false; }

  bool NonNestableTasksEnabled() const override { return true; }

#if SCRIPTX_V8_VERSION_BETWEEN(7, 5, 12, 4)
  void PostNonNestableDelayedTask(std::unique_ptr<v8::Task> task,
                                  double delay_in_seconds) override {
    scheduleTask(std::move(task), delay_in_seconds);
  }
#endif

#if SCRIPTX_V8_VERSION_GE(7, 5)
  bool NonNestableDelayedTasksEnabled() const override { return true; }
#endif

 private:
  void scheduleTask(std::unique_ptr<v8::Task> task, double delay_in_seconds = 0) {
    if (engine_->isDestroying()) {
      return;
    }

    script::utils::Message s(
        [](auto& msg) {
          auto engine = static_cast<V8Engine*>(msg.tag);
          EngineScope scope(engine);
          try {
            static_cast<v8::Task*>(msg.ptr0)->Run();
          } catch (const Exception& e) {
            // this should not happen, all JS exceptions should be handled by V8
            abort();
          }
        },
        [](auto& msg) {
          using deleter = std::unique_ptr<v8::Task>::deleter_type;
          deleter{}(static_cast<v8::Task*>(msg.ptr0));
        });
    s.name = "SchedulePump";
    s.ptr0 = task.release();
    s.tag = engine_;

    engine_->messageQueue()->postMessage(s, std::chrono::duration<double>(delay_in_seconds));
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

#if SCRIPTX_V8_VERSION_GE(10, 0)
  v8::V8::DisposePlatform();
#else
  // DEPRECATED in 10.0 36707481ffa
  v8::V8::ShutdownPlatform();
#endif
}

#if SCRIPTX_V8_VERSION_GE(11, 7)
std::shared_ptr<v8::TaskRunner> V8Platform::GetForegroundTaskRunner(v8::Isolate* isolate,
                                                                    v8::TaskPriority priority) {
#else
std::shared_ptr<v8::TaskRunner> V8Platform::GetForegroundTaskRunner(v8::Isolate* isolate) {
#endif
  std::lock_guard<std::mutex> lock(lock_);
  auto queueRunner = engineMap_[isolate].messageQueueRunner;
  if (!queueRunner->inited()) {
    // this method may be called during creating Isolate...
    // set anything we now ASAP
    queueRunner->setQueue(isolate);
  }

  return queueRunner;
}

void V8Platform::OnCriticalMemoryPressure() {
  Logger() << "V8Platform::OnCriticalMemoryPressure()";
  return defaultPlatform_->OnCriticalMemoryPressure();
}

#if SCRIPTX_V8_VERSION_LE(10, 6)
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
