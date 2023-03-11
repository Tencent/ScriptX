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

#pragma once

#include <string>

#include "Engine.h"
#include "Exception.h"
#include "Reference.h"
#include SCRIPTX_BACKEND(Engine.h)
#include SCRIPTX_BACKEND(Includes.h)

namespace script {

template <typename T>
void ScriptEngine::registerNativeClass(const ClassDefine<T>& classDefine) {
  return registerNativeClassInternal(
      internal::typeIndexOf<T>(), static_cast<const internal::ClassDefineState*>(&classDefine),
      [](void* instancePointer) {
        return static_cast<ScriptClass*>(static_cast<T*>(instancePointer));
      });
}

template <typename T>
const ClassDefine<T>& ScriptEngine::getClassDefine() const {
  static_assert(!std::is_same_v<void, T>);
  auto state = getClassDefineInternal(internal::typeIndexOf<T>());
  return *static_cast<const ClassDefine<T>*>(state);
}

template <typename T>
Local<Object> ScriptEngine::newNativeClass(const std::vector<Local<Value>>& args) {
  static_assert(!std::is_same_v<void, T>);
  return performNewNativeClass(internal::typeIndexOf<T>(), &getClassDefine<T>(), args.size(),
                               args.data());
}

template <typename T>
Local<Object> ScriptEngine::newNativeClass(const std::initializer_list<Local<Value>>& args) {
  static_assert(!std::is_same_v<void, T>);
  return performNewNativeClass(internal::typeIndexOf<T>(), &getClassDefine<T>(), args.size(),
                               args.begin());
}

template <typename T>
bool ScriptEngine::isInstanceOf(const Local<script::Value>& value) {
  return performIsInstanceOf(value, &getClassDefine<T>());
}

template <typename T>
T* ScriptEngine::getNativeInstance(const Local<script::Value>& value) {
  return static_cast<T*>(performGetNativeInstance(value, &getClassDefine<T>()));
}

template <typename T>
inline std::shared_ptr<T> ScriptEngine::getData() {
  return std::static_pointer_cast<T>(userData_);
}

}  // namespace script