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
#include "../../src/Native.hpp"
#include "QjsEngine.h"
#include "QjsHelper.hpp"

namespace script::qjs_backend {

template <typename T>
void QjsEngine::registerNativeClassImpl(const ClassDefine<T>* classDefine) {
  auto ns = getNamespaceForRegister(classDefine->getNameSpace());

  auto hasInstance = classDefine->instanceDefine.constructor;

  // static only
  auto module = hasInstance ? newConstructor(*classDefine) : Object::newObject();
  registerNativeStatic(module, classDefine->staticDefine);

  if (hasInstance) {
    auto proto = newPrototype(*classDefine);
    nativeInstanceRegistry_.template emplace(&classDefine, qjs_interop::getLocal(proto, context_));
  }
  ns.set(classDefine->className, module);
}

template <typename T>
Local<Object> QjsEngine::newConstructor(const ClassDefine<T>& classDefine) const {
  return newRawFunction(
             context_, const_cast<ClassDefine<T>*>(&classDefine),
             [](const Arguments& args, void* data, bool isConstructorCall) {
               if (!isConstructorCall) {
                 throw Exception(u8"constructor can't be called as function");
               }

               auto classDefine = static_cast<const ClassDefine<T>*>(data);
               auto engine = args.template engineAs<QjsEngine>();
               auto proto = engine->nativeInstanceRegistry_.find(classDefine);
               assert(proto != engine->nativeInstanceRegistry_.end());

               auto obj = JS_NewObjectClass(engine->context_, kInstanceClassId);
               auto ret = JS_SetPrototype(engine->context_, obj,
                                          qjs_backend::dupValue(proto->second, engine->context_));
               checkException(ret);

               auto callbackInfo = args.callbackInfo_;
               callbackInfo.thiz_ = obj;

               auto ptr = classDefine->instanceDefine.constructor(Arguments(callbackInfo));
               if (ptr == nullptr) {
                 throw Exception("can't create class " + classDefine->className);
               }
               JS_SetOpaque(obj, ptr);

               return qjs_interop::makeLocal<Value>(obj);
             })
      .asValue()
      .asObject();
}

template <typename T>
Local<Object> QjsEngine::newPrototype(const ClassDefine<T>& define) const {
  auto proto = Object::newObject();
  using IDT = internal::InstanceDefine<T>;

  auto& def = define.instanceDefine;
  for (auto&& f : def.functions) {
    using FCT = typename IDT::FunctionDefine::FunctionCallback;
    auto ptr = &f.callback;

    auto fun = newRawFunction(
        context_, const_cast<FCT*>(&f.callback), [](const Arguments& args, void* func_data, bool) {
          auto ptr =
              static_cast<T*>(JS_GetOpaque(qjs_interop::peekLocal(args.thiz()), kInstanceClassId));
          return (*static_cast<FCT*>(func_data))(ptr, args);
        });
    proto.set(f.name, fun);
  }

  for (auto&& prop : def.properties) {
    using GCT = typename IDT::PropertyDefine::GetterCallback;
    using SCT = typename IDT::PropertyDefine::SetterCallback;

    auto getterFun = newRawFunction(
        context_, const_cast<GCT*>(&prop.getter), [](const Arguments& args, void* data, bool) {
          auto ptr =
              static_cast<T*>(JS_GetOpaque(qjs_interop::peekLocal(args.thiz()), kInstanceClassId));
          return (*static_cast<GCT*>(data))(ptr);
        });

    auto setterFun = newRawFunction(
        context_, const_cast<SCT*>(&prop.setter), [](const Arguments& args, void* data, bool) {
          auto ptr =
              static_cast<T*>(JS_GetOpaque(qjs_interop::peekLocal(args.thiz()), kInstanceClassId));
          (*static_cast<SCT*>(data))(ptr, args[0]);
          return Local<Value>();
        });

    auto atom = JS_NewAtomLen(context_, prop.name.c_str(), prop.name.length());

    // TODO: flags
    auto ret = JS_DefinePropertyGetSet(context_, qjs_interop::peekLocal(proto), atom,
                                       qjs_interop::getLocal(getterFun),
                                       qjs_interop::getLocal(setterFun), 0);
    JS_FreeAtom(context_, atom);
    qjs_backend::checkException(ret);
  }
  return proto;
}

}  // namespace script::qjs_backend