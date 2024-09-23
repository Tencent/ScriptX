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

// for all v8 api changes, refer to https://github.com/LanderlYoung/ScriptXTestLibs/blob/main/v8/

#pragma once

#include <mutex>
#include <unordered_map>

#include "../../src/foundation.h"

SCRIPTX_BEGIN_INCLUDE_LIBRARY
#include <libplatform/libplatform.h>
#include <v8-platform.h>
SCRIPTX_END_INCLUDE_LIBRARY

#include "V8Helper.h"

namespace script::v8_backend {

class V8Engine;

class MessageQueueTaskRunner;

class V8Platform : public v8::Platform {
 private:
  std::unique_ptr<v8::Platform> defaultPlatform_;

  V8Platform();

  SCRIPTX_DISALLOW_COPY_AND_MOVE(V8Platform);

 public:
#if SCRIPTX_V8_VERSION_GE(11, 7)
  // V7.1: added 1-param one
  // V11.7: added 2-param overload one, 1-param one delegate to 2-param one
  // V13.0: made 1-param overload one non-virtual
  std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner(v8::Isolate* isolate,
                                                          v8::TaskPriority priority) override;
#else
  std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner(v8::Isolate* isolate) override;
#endif

  void OnCriticalMemoryPressure() override;

#if SCRIPTX_V8_VERSION_LE(10, 6)
  // DEPRECATED in 10.7 - 24cf9b
  // REMOVED in 10.8 - 8b8703
  bool OnCriticalMemoryPressure(size_t length) override;
#endif

  // directly delegate to default platform
  int NumberOfWorkerThreads() override { return defaultPlatform_->NumberOfWorkerThreads(); }

#if SCRIPTX_V8_VERSION_LE(11, 3)  // added default impl to call PostTaskOnWorkerThreadImpl in 11.4
  void CallOnWorkerThread(std::unique_ptr<v8::Task> task) override {
    return defaultPlatform_->CallOnWorkerThread(std::move(task));
  }

  void CallDelayedOnWorkerThread(std::unique_ptr<v8::Task> task, double delay_in_seconds) override {
    return defaultPlatform_->CallDelayedOnWorkerThread(std::move(task), delay_in_seconds);
  }
#endif

  double MonotonicallyIncreasingTime() override {
    return defaultPlatform_->MonotonicallyIncreasingTime();
  }

  double CurrentClockTimeMillis() override { return defaultPlatform_->CurrentClockTimeMillis(); }

  v8::TracingController* GetTracingController() override {
    return defaultPlatform_->GetTracingController();
  }

  v8::PageAllocator* GetPageAllocator() override { return defaultPlatform_->GetPageAllocator(); }

  // v8::Platform::PostJob, introduced since 8.4
  // https://chromium.googlesource.com/v8/v8/+/05b6268126c1435d1c964ef81799728088b72c76
  // NOTE: not available in node 14.x (node.js modified v8 code...)
  // https://nodejs.org/en/download/releases/
  // and node 15.x uses v8 8.6+
  // V8 8.6 made it pure virtual
  // V8 10.5 add default impl to CreateJob
  // V12.2 make it none-virtual by delegate to CreateJobImpl
#if defined(BUILDING_NODE_EXTENSION) ? SCRIPTX_V8_VERSION_BETWEEN(8, 6, 12, 1) \
                                     : SCRIPTX_V8_VERSION_BETWEEN(8, 4, 12, 1)
  virtual std::unique_ptr<v8::JobHandle> PostJob(v8::TaskPriority priority,
                                                 std::unique_ptr<v8::JobTask> job_task) override {
    return defaultPlatform_->PostJob(priority, std::move(job_task));
  }
#endif

#if SCRIPTX_V8_VERSION_BETWEEN(10, 5, 11, 3)
  // V10.5 added pure-virtual
  // V11.4 added default impl to CreateJobImpl
  // V12.2 make it none-virtual by delegate to CreateJobImpl
  std::unique_ptr<v8::JobHandle> CreateJob(v8::TaskPriority priority,
                                           std::unique_ptr<v8::JobTask> job_task) override {
    return defaultPlatform_->CreateJob(priority, std::move(job_task));
  }
#endif

#if SCRIPTX_V8_VERSION_GE(11, 4)
 protected:
  // V11.4 added
  // V12.2 made pure virtual
  virtual std::unique_ptr<v8::JobHandle> CreateJobImpl(
      v8::TaskPriority priority, std::unique_ptr<v8::JobTask> job_task,
      const v8::SourceLocation& location) override {
    return defaultPlatform_->CreateJob(priority, std::move(job_task));
  }

  // V11.4 added
  // V12.2 made pure virtual
  virtual void PostTaskOnWorkerThreadImpl(v8::TaskPriority priority, std::unique_ptr<v8::Task> task,
                                          const v8::SourceLocation& location) override {
#if SCRIPTX_V8_VERSION_GE(12, 2)
    defaultPlatform_->CallOnWorkerThread(std::move(task), location);
#else
    defaultPlatform_->CallOnWorkerThread(std::move(task));
#endif
  }

  // V11.4 added
  // V12.2 made pure virtual
  virtual void PostDelayedTaskOnWorkerThreadImpl(v8::TaskPriority priority,
                                                 std::unique_ptr<v8::Task> task,
                                                 double delay_in_seconds,
                                                 const v8::SourceLocation& location) override {
#if SCRIPTX_V8_VERSION_GE(12, 2)
    defaultPlatform_->CallDelayedOnWorkerThread(std::move(task), delay_in_seconds, location);
#else
    defaultPlatform_->CallDelayedOnWorkerThread(std::move(task), delay_in_seconds);
#endif
  }

 public:
#endif

#if SCRIPTX_V8_VERSION_LE(8, 0)  // removed in 8.1
  void CallOnForegroundThread(v8::Isolate* isolate, v8::Task* task) override {
    return GetForegroundTaskRunner(isolate)->PostTask(std::unique_ptr<v8::Task>(task));
  }

  void CallDelayedOnForegroundThread(v8::Isolate* isolate, v8::Task* task,
                                     double delay_in_seconds) override {
    return GetForegroundTaskRunner(isolate)->PostDelayedTask(std::unique_ptr<v8::Task>(task),
                                                             delay_in_seconds);
  }
#endif

  bool IdleTasksEnabled(v8::Isolate* isolate) override { return false; }

#if SCRIPTX_V8_VERSION_GE(11, 3)
  virtual std::unique_ptr<v8::ScopedBlockingCall> CreateBlockingScope(
      v8::BlockingType blocking_type) override {
    return defaultPlatform_->CreateBlockingScope(blocking_type);
  }
#endif

 public:
  ~V8Platform() override;

 private:
  static std::mutex lock_;
  static std::shared_ptr<V8Platform> singletonInstance_;

  struct EngineData {
    // auto created in the default ctor
    std::shared_ptr<MessageQueueTaskRunner> messageQueueRunner;
    EngineData();
  };

  std::unordered_map<v8::Isolate*, EngineData> engineMap_;

 public:
  /**
   * @return the VERY ONE AND ONLY platform
   */
  static std::shared_ptr<V8Platform> getPlatform();

  void addEngineInstance(v8::Isolate* isolate, V8Engine* engine);

  void removeEngineInstance(v8::Isolate* isolate);

  friend struct script::v8_interop;
};

}  // namespace script::v8_backend
