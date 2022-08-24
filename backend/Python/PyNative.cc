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

Local<Object> Arguments::thiz() const { return py_interop::toLocal<Object>(callbackInfo_.self); }

bool Arguments::hasThiz() const { return callbackInfo_.self; }

size_t Arguments::size() const { return PyTuple_Size(callbackInfo_.args); }

Local<Value> Arguments::operator[](size_t i) const {
  return py_interop::toLocal<Value>(PyTuple_GetItem(callbackInfo_.args, i));
}

ScriptEngine* Arguments::engine() const { return callbackInfo_.engine; }

ScriptClass::ScriptClass(const Local<Object>& scriptObject) : internalState_() {
  internalState_.engine = &py_backend::currentEngineChecked();
}

Local<Object> ScriptClass::getScriptObject() const {
  return py_interop::toLocal<Object>(internalState_.script_obj);
}

Local<Array> ScriptClass::getInternalStore() const {
  return py_interop::toLocal<Array>(internalState_.storage);
}

ScriptEngine* ScriptClass::getScriptEngine() const { return internalState_.engine; }

ScriptClass::~ScriptClass(){};
}  // namespace script