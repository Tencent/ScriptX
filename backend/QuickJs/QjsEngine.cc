/*
 * Tencent is pleased to support the open source community by making ScriptX available.
 * Copyright (C) 2021 THL A29 Limited, a Tencent company.  All rights reserved.
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

#include <ScriptX/ScriptX.h>

namespace script::qjs_backend {

JSClassID QjsEngine::kPointerClassId = 0;
JSClassID QjsEngine::kFunctionDataClassId = 0;
static std::once_flag kGlobalQjsClass;

QjsEngine::QjsEngine(std::shared_ptr<utils::MessageQueue> queue, const QjsFactory& factory)
    : queue_(queue ? std::move(queue) : std::make_shared<utils::MessageQueue>()) {
  std::call_once(kGlobalQjsClass, []() { JS_NewClassID(&kPointerClassId); });

  if (factory) {
    std::tie(runtime_, context_) = factory();
    assert(runtime_);
    assert(context_);
  } else {
    runtime_ = JS_NewRuntime();
    assert(runtime_);
    context_ = JS_NewContext(runtime_);
    assert(context_);
  }

  initEngineResource();
}

void QjsEngine::initEngineResource() {
  JS_SetRuntimeOpaque(runtime_, this);

  JSClassDef pointer{};
  pointer.class_name = "RawPointer";
  JS_NewClass(runtime_, kPointerClassId, &pointer);

  JSClassDef function{};
  function.class_name = "RawFunction";
  function.finalizer = [](JSRuntime* /*rt*/, JSValue val) {
    auto ptr = JS_GetOpaque(val, kFunctionDataClassId);
    if (ptr) {
      delete static_cast<FunctionCallback*>(ptr);
    }
  };
  JS_NewClass(runtime_, kFunctionDataClassId, &function);

  lengthAtom_ = JS_NewAtom(context_, "length");
}

QjsEngine::~QjsEngine() = default;

void QjsEngine::destroy() noexcept {
  ScriptEngine::destroyUserData();

  JS_FreeAtom(context_, lengthAtom_);
  JS_RunGC(runtime_);
  JS_FreeContext(context_);
  JS_FreeRuntime(runtime_);
}

Local<Value> QjsEngine::get(const Local<String>& key) { return Local<Value>(); }

void QjsEngine::set(const Local<String>& key, const Local<Value>& value) {}

Local<Object> QjsEngine::getGlobal() const {
  auto global = JS_GetGlobalObject(context_);
  qjs_backend::checkException(global);
  return qjs_interop::makeLocal<Object>(global);
}

Local<Value> QjsEngine::eval(const Local<String>& script) { return eval(script, Local<Value>()); }

Local<Value> QjsEngine::eval(const Local<String>& script, const Local<String>& sourceFile) {
  return eval(script, sourceFile.asValue());
}

Local<Value> QjsEngine::eval(const Local<String>& script, const Local<Value>& sourceFile) {
  return Local<Value>();
}

std::shared_ptr<utils::MessageQueue> QjsEngine::messageQueue() {
  return std::shared_ptr<utils::MessageQueue>();
}

void QjsEngine::gc() {}

void QjsEngine::adjustAssociatedMemory(int64_t count) {}

ScriptLanguage QjsEngine::getLanguageType() { return ScriptLanguage::kJavaScript; }

std::string QjsEngine::getEngineVersion() { return ""; }

bool QjsEngine::isDestroying() const { return false; }

}  // namespace script::qjs_backend
