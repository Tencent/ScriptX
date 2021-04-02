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

namespace script {

Arguments::Arguments(InternalCallbackInfoType callbackInfo) : callbackInfo_(callbackInfo) {}

Arguments::~Arguments() = default;

Local<Object> Arguments::thiz() const {
  return qjs_interop::makeLocal<Value>(
             qjs_backend::dupValue(callbackInfo_.thiz_, callbackInfo_.engine_->context_))
      .asObject();
}

bool Arguments::hasThiz() const { return JS_IsObject(callbackInfo_.thiz_); }

size_t Arguments::size() const { return callbackInfo_.argc_; }

Local<Value> Arguments::operator[](size_t i) const {
  if (i >= callbackInfo_.argc_) {
    return {};
  }
  return qjs_interop::makeLocal<Value>(
      qjs_backend::dupValue(callbackInfo_.argv_[i], callbackInfo_.engine_->context_));
}

ScriptEngine* Arguments::engine() const { return callbackInfo_.engine_; }

ScriptClass::ScriptClass(const script::Local<script::Object>& scriptObject) : internalState_() {
  internalState_.engine = &qjs_backend::currentEngine();
  // don't inc reference count, to pretend to be a weak ref
  // while they are not exactly weak ref are defined.
  // BUT, QuickJs calls finalize immediately, we can clear it there.
  //  internalState_.weakRef_ = qjs_interop::peekLocal(scriptObject);
}

Local<Object> ScriptClass::getScriptObject() const {
  //  if (JS_IsObject(internalState_.weakRef_)) {
  //    return qjs_interop::makeLocal<Object>(
  //        qjs_backend::dupValue(internalState_.weakRef_, internalState_.engine->context_));
  //  }
  // TODO: to be or not to be???
  throw Exception("can't getScriptObject in finalizer");
}

Local<Array> ScriptClass::getInternalStore() const { TEMPLATE_NOT_IMPLEMENTED(); }

ScriptEngine* ScriptClass::getScriptEngine() const { return internalState_.engine; }

ScriptClass::~ScriptClass() = default;

}  // namespace script