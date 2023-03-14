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

namespace internal {

void ClassDefineState::validateClassDefine(bool isBaseOfScriptClass) const {
  auto classDefine = this;
  auto throwException = [classDefine](const char* msg) {
    std::string info = msg;
    if (classDefine) {
      info = "failed to valid class define [" + classDefine->className + "] " + msg;
    }
    if (EngineScope::currentEngine()) {
      throw Exception(info);
    } else {
      throw std::runtime_error(info);
    }
  };

  if (classDefine == nullptr) {
    throwException("null class define");
  }

  if (classDefine->className.empty()) {
    throwException("empty class name");
  }

  bool hasStatic =
      !classDefine->staticDefine.functions.empty() || !classDefine->staticDefine.properties.empty();

  bool hasInstance = static_cast<bool>(classDefine->instanceDefine.constructor) ||
                     !classDefine->instanceDefine.functions.empty() ||
                     !classDefine->instanceDefine.properties.empty();

  if (!hasStatic && !hasInstance) {
    throwException("both static and instance define are empty");
  }

  if (hasStatic) {
    for (auto funcDef : classDefine->staticDefine.functions) {
      if (funcDef.name.empty()) {
        throwException("staticDefine.functions has no name");
      }
      if (funcDef.callback == nullptr) {
        throwException("staticDefine.functions has no callback");
      }
    }

    for (auto propDef : classDefine->staticDefine.properties) {
      if (propDef.name.empty()) {
        throwException("staticDefine.properties has no name");
      }
      if (propDef.getter == nullptr && propDef.setter == nullptr) {
        throwException("staticDefine.functions has no getter&setter");
      }
    }
  }

  if (classDefine->instanceDefine.constructor) {
    if (!isBaseOfScriptClass) {
      throwException("ClassDefine with instance must have a valid type parameter");
    }
    for (auto funcDef : classDefine->instanceDefine.functions) {
      if (funcDef.name.empty()) {
        throwException("instanceDefine.functions has no name");
      }
      if (funcDef.callback == nullptr) {
        throwException("instanceDefine.functions has no callback");
      }
    }

    for (auto propDef : classDefine->instanceDefine.properties) {
      if (propDef.name.empty()) {
        throwException("instanceDefine.functions has no name");
      }
      if (propDef.getter == nullptr && propDef.setter == nullptr) {
        throwException("instanceDefine.functions has no getter&setter");
      }
    }
  } else {
    if (!classDefine->instanceDefine.properties.empty() ||
        !classDefine->instanceDefine.functions.empty()) {
      throwException("instance has no constructor");
    }
  }
}

#ifdef __cpp_rtti

void ClassDefineState::visit(script::ClassDefineVisitor& visitor) const {
  visitor.beginClassDefine(className, nameSpace);

  for (auto&& prop : staticDefine.properties) {
    visitor.visitStaticProperty(prop.name, prop.getter.target_type(), prop.setter.target_type());
  }
  for (auto&& function : staticDefine.functions) {
    visitor.visitStaticFunction(function.name, function.callback.target_type());
  }

  if (instanceDefine.constructor) {
    visitor.visitConstructor(instanceDefine.constructor.target_type());
  }

  for (auto&& prop : instanceDefine.properties) {
    visitor.visitInstanceProperty(prop.name, prop.getter.target_type(), prop.setter.target_type());
  }
  for (auto&& function : instanceDefine.functions) {
    visitor.visitInstanceFunction(function.name, function.callback.target_type());
  }

  visitor.endClassDefine();
}

#endif

}  // namespace internal

}  // namespace script