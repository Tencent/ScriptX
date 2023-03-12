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

#pragma once

#include "../../src/Engine.h"
#include "../../src/Exception.h"
#include "../../src/utils/MessageQueue.h"

namespace script::template_backend {

class TemplateEngine : public ScriptEngine {
 protected:
 public:
  TemplateEngine(std::shared_ptr<::script::utils::MessageQueue> queue);

  TemplateEngine();

  SCRIPTX_DISALLOW_COPY_AND_MOVE(TemplateEngine);

  void destroy() noexcept override;

  bool isDestroying() const override;

  Local<Value> get(const Local<String>& key) override;

  void set(const Local<String>& key, const Local<Value>& value) override;
  using ScriptEngine::set;

  Local<Value> eval(const Local<String>& script, const Local<Value>& sourceFile);
  Local<Value> eval(const Local<String>& script, const Local<String>& sourceFile) override;
  Local<Value> eval(const Local<String>& script) override;
  using ScriptEngine::eval;

  std::shared_ptr<utils::MessageQueue> messageQueue() override;

  void gc() override;

  void adjustAssociatedMemory(int64_t count) override;

  ScriptLanguage getLanguageType() override;

  std::string getEngineVersion() override;

 protected:
  ~TemplateEngine() override;

  void performRegisterNativeClass(
      internal::TypeIndex typeIndex, const internal::ClassDefineState* classDefine,
      script::ScriptClass* (*instanceTypeToScriptClass)(void*)) override {
    TEMPLATE_NOT_IMPLEMENTED();
  }

  Local<Object> performNewNativeClass(internal::TypeIndex typeIndex,
                                      const internal::ClassDefineState* classDefine, size_t size,
                                      const Local<script::Value>* args) override {
    TEMPLATE_NOT_IMPLEMENTED();
  }

  void* performGetNativeInstance(const Local<script::Value>& value,
                                 const internal::ClassDefineState* classDefine) override {
    TEMPLATE_NOT_IMPLEMENTED();
  }

  bool performIsInstanceOf(const Local<script::Value>& value,
                           const internal::ClassDefineState* classDefine) override {
    TEMPLATE_NOT_IMPLEMENTED();
  }

 private:
  template <typename T>
  friend class ::script::Local;

  template <typename T>
  friend class ::script::Global;

  template <typename T>
  friend class ::script::Weak;

  friend class ::script::Object;

  friend class ::script::Array;

  friend class ::script::Function;

  friend class ::script::ByteBuffer;

  friend class ::script::ScriptEngine;

  friend class ::script::Exception;

  friend class ::script::Arguments;

  friend class ::script::ScriptClass;
};

}  // namespace script::template_backend