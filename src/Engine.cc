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

#include <ScriptX/ScriptX.h>

namespace script {

void ScriptEngine::setData(std::shared_ptr<void> arbitraryData) {
  userData_ = std::move(arbitraryData);
}

void ScriptEngine::destroyUserData() { userData_.reset(); }

void ScriptEngine::registerNativeClass(const script::NativeRegister& nativeRegister) {
  nativeRegister.registerNativeClass(this);
}

void ScriptEngine::registerNativeClassInternal(
    internal::TypeIndex typeIndex, const internal::ClassDefineState* classDefine,
    script::ScriptClass* (*instanceTypeToScriptClass)(void*)) {
  if ((!classDefine->hasInstanceDefine() &&
       staticClassDefineRegistry_.find(classDefine) != staticClassDefineRegistry_.end()) ||
      classDefineRegistry_.find(typeIndex) != classDefineRegistry_.end()) {
    throw Exception(std::string("already registered for " + classDefine->className));
  }
  performRegisterNativeClass(typeIndex, classDefine, instanceTypeToScriptClass);

  if (!classDefine->hasInstanceDefine()) {
    staticClassDefineRegistry_.emplace(classDefine);
  } else {
    classDefineRegistry_.emplace(typeIndex, classDefine);
  }
}

const internal::ClassDefineState* ScriptEngine::getClassDefineInternal(
    internal::TypeIndex typeIndex) const {
  auto it = classDefineRegistry_.find(typeIndex);
  if (it == classDefineRegistry_.end()) {
    throw Exception(std::string("ClassDefine is not registered"));
  }
  return it->second;
}

}  // namespace script
