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

#include "../../src/Native.hpp"
#include "PyEngine.h"
#include "PyHelper.hpp"

namespace script {

Arguments::Arguments(InternalCallbackInfoType callbackInfo) : callbackInfo_(callbackInfo) {}

Arguments::~Arguments() = default;

Local<Object> Arguments::thiz() const {
  return py_interop::makeLocal<Value>(callbackInfo_.self).asObject();
}

bool Arguments::hasThiz() const { return callbackInfo_.self != nullptr; }

size_t Arguments::size() const {
  if (!callbackInfo_.args) {
    return 0;
  }
  return PyTuple_Size(callbackInfo_.args);
}

Local<Value> Arguments::operator[](size_t i) const {
  if (i < size()) {
    return py_interop::makeLocal<Value>(PyTuple_GetItem(callbackInfo_.args, i));
  }
  return {};
}

ScriptEngine* Arguments::engine() const { return callbackInfo_.engine; }

ScriptClass::ScriptClass(const script::Local<script::Object>& scriptObject) : internalState_() {
  TEMPLATE_NOT_IMPLEMENTED();
}

Local<Object> ScriptClass::getScriptObject() const { TEMPLATE_NOT_IMPLEMENTED(); }

Local<Array> ScriptClass::getInternalStore() const { TEMPLATE_NOT_IMPLEMENTED(); }

ScriptEngine* ScriptClass::getScriptEngine() const { TEMPLATE_NOT_IMPLEMENTED(); }

ScriptClass::~ScriptClass() = default;
}  // namespace script