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

#include "WasmEngine.h"

#include "../../src/Native.hpp"
#include "../../src/Reference.h"
#include "../../src/Utils.h"
#include "WasmNative.hpp"
#include "WasmReference.hpp"
#include "WasmScope.hpp"

namespace script::wasm_backend {

WasmEngine::WasmEngine() { engineThreadId_ = std::this_thread::get_id(); }

WasmEngine* WasmEngine::instance() {
  static WasmEngine engine;
  return &engine;
}

WasmEngine::~WasmEngine() = default;

void WasmEngine::ignoreDestroyCall() { instance()->ignoreDestroyCall_ = true; }

void WasmEngine::destroy() {
  if (!ignoreDestroyCall_) {
    throw Exception(
        "WasmEngine is a Singleton, thus can't be destroyed. "
        "To suppress this exception, please opt-in with WasmEngine::ignoreDestroyCall()");
  }
}

bool WasmEngine::isDestroying() const { return false; }

void WasmEngine::unitTestResetRegistry() {
  classDefineRegistry_.clear();
  ScriptEngine::classDefineRegistry_.clear();
}

void WasmEngine::doDeleteScriptClass(ScriptClass* scriptClass) { delete scriptClass; }

Local<Object> WasmEngine::getGlobal() const { return Local<Object>(Stack::getGlobal()); }

Local<Value> WasmEngine::get(const Local<String>& key) {
  return Local<Value>(Stack::globalGet(key.val_));
}

void WasmEngine::set(const Local<String>& key, const Local<Value>& value) {
  Stack::globalSet(key.val_, value.val_);
}

Local<Value> WasmEngine::eval(const Local<String>& script) { return eval(script, Local<Value>()); }

Local<Value> WasmEngine::eval(const Local<String>& script, const Local<String>& sourceFile) {
  return eval(script, sourceFile.asValue());
}

Local<Value> WasmEngine::eval(const Local<String>& script, const Local<Value>& sourceFile) {
  Tracer trace(this, "WasmEngine::eval");
  auto retIndex = evaluateJavaScriptCode(script.val_, sourceFile.val_);
  return Local<Value>(retIndex);
}

std::shared_ptr<utils::MessageQueue> WasmEngine::messageQueue() { return messageQueue_; }

void WasmEngine::gc() {}

void WasmEngine::adjustAssociatedMemory(int64_t count) {}

ScriptLanguage WasmEngine::getLanguageType() { return ScriptLanguage::kJavaScript; }

std::string WasmEngine::getEngineVersion() { return "WebAssemble"; }

void WasmEngine::defineStatic(const Local<Object>& obj, const internal::StaticDefine& define) {
  for (auto&& func : define.functions) {
    StackFrameScope stackFrame;
    auto fi = Stack::newFunction(
        [](const Arguments& args, void* data, void*) -> Local<Value> {
          auto fun = static_cast<internal::StaticDefine::FunctionDefine*>(data);
          Tracer trace(args.engineAs<WasmEngine>(), fun->traceName);
          return fun->callback(args);
        },
        &func);
    obj.set(func.name, Local<Value>(fi));
  }

  for (auto&& prop : define.properties) {
    StackFrameScope stackFrame;

    int getter = -1;
    int setter = -1;
    auto name = String::newString(prop.name);

    if (prop.getter) {
      getter = Stack::newFunction(
          [](const Arguments& args, void* data, void*) -> Local<Value> {
            auto pro = static_cast<internal::StaticDefine::PropertyDefine*>(data);
            Tracer trace(args.engine(), pro->traceName);
            return pro->getter();
          },
          &prop);
    }

    if (prop.setter) {
      setter = Stack::newFunction(
          [](const Arguments& args, void* data, void*) -> Local<Value> {
            auto pro = static_cast<internal::StaticDefine::PropertyDefine*>(data);
            Tracer trace(args.engine(), pro->traceName);
            pro->setter(args[0]);
            return {};
          },
          &prop);
    }
    NativeHelper::defineProperty(obj.val_, name.val_, getter, setter);
  }
}

void* WasmEngine::verifyAndGetInstance(const void* classDefine, int thiz) {
  auto ins = NativeHelper::getInternalStateInstance(thiz);
  auto define = NativeHelper::getInternalStateClassDefine(thiz);
  if (ins == nullptr || define != classDefine) {
    throw Exception("call Instance Function on wrong instance");
  }
  return ins;
}

Local<Object> WasmEngine::getNamespaceForRegister(const std::string_view& nameSpace) {
  StackFrameScope scope;
  auto ret = NativeHelper::getNamespaceForRegister(String::newString(nameSpace).val_);
  if (ret == -1) {
    throw Exception("can't get namespace for:" + std::string(nameSpace));
  }
  return scope.returnValue(Local<Value>(ret).asObject());
}

// Native

void WasmEngine::performRegisterNativeClass(
    internal::TypeIndex typeIndex, const internal::ClassDefineState* classDefine,
    script::ScriptClass* (*instanceTypeToScriptClass)(void*)) {
  if (classDefineRegistry_.find(classDefine) != classDefineRegistry_.end()) {
    throw Exception("classDefine [" + classDefine->className + "] already registered");
  }

  StackFrameScope scope;

  auto hasInstance = classDefine->hasInstanceDefine();
  auto obj = hasInstance ? newConstructor(classDefine) : Object::newObject();

  defineStatic(obj, classDefine->staticDefine);

  if (hasInstance) {
    defineInstance(classDefine, obj);
  }

  auto ns = getNamespaceForRegister(classDefine->nameSpace);
  ns.set(classDefine->className, obj);

  classDefineRegistry_.emplace(classDefine, obj);
}

Local<Object> WasmEngine::performNewNativeClass(internal::TypeIndex typeIndex,
                                                const internal::ClassDefineState* classDefine,
                                                size_t size, const Local<script::Value>* args) {
  auto it = classDefineRegistry_.find(classDefine);
  if (it == classDefineRegistry_.end()) {
    throw Exception("classDefine [" + classDefine->className + "] is not registered");
  }

  StackFrameScope scope;
  auto ctor = it->second.get();
  auto ret = Object::newObjectImpl(ctor, size, args);
  return scope.returnValue(ret);
}

bool WasmEngine::performIsInstanceOf(const Local<script::Value>& value,
                                     const internal::ClassDefineState* classDefine) {
  return NativeHelper::getInternalStateClassDefine(value.val_) == classDefine;
}

void* WasmEngine::performGetNativeInstance(const Local<script::Value>& value,
                                           const internal::ClassDefineState* classDefine) {
  if (performIsInstanceOf(value, classDefine)) {
    return static_cast<void*>(NativeHelper::getInternalStateInstance(value.val_));
  }
  return nullptr;
}

Local<Object> WasmEngine::newConstructor(const internal::ClassDefineState* classDefine) {
  auto ctor = Stack::newFunction(
      [](const Arguments& args, void* data, void*) -> Local<Value> {
        auto classDefine = static_cast<internal::ClassDefineState*>(data);

        Tracer trace(args.engine(), classDefine->className);
        void* instance;
        if (args.size() == 2 && NativeHelper::isCppNewMark(args[0].val_)) {
          // WASM is 32-bit, we could use int32_t to store a pointer.
          // we have static assert in WasmEngine.h
          instance = reinterpret_cast<void*>(args[1].asNumber().toInt32());
        } else {
          instance = classDefine->instanceDefine.constructor(args);
          if (instance == nullptr) {
            throw Exception("can't create class " + classDefine->className);
          }
        }

        NativeHelper::setInternalState(args.thiz().val_, classDefine, instance);
        return {};
      },
      classDefine, nullptr, true);
  return Local<Object>(ctor);
}

void WasmEngine::defineInstance(const internal::ClassDefineState* classDefine,
                                const Local<Object>& obj) {
  auto&& instanceDefine = classDefine->instanceDefine;
  auto prototype = Object::newObject();

  for (auto&& func : instanceDefine.functions) {
    StackFrameScope stack;
    auto fi = Stack::newFunction(
        [](const Arguments& args, void* data0, void* data1) -> Local<Value> {
          auto classDefine = static_cast<const internal::ClassDefineState*>(data0);
          auto& func = *static_cast<typename internal::InstanceDefine::FunctionDefine*>(data1);

          auto ins = verifyAndGetInstance(classDefine, args.thiz().val_);

          Tracer trace(args.engine(), func.traceName);
          return func.callback(ins, args);
        },
        classDefine, &func);

    prototype.set(func.name, Local<Value>(fi));
  }

  for (auto&& prop : instanceDefine.properties) {
    StackFrameScope stackFrame;

    int getter = -1;
    int setter = -1;
    auto name = String::newString(prop.name);

    if (prop.getter) {
      getter = Stack::newFunction(
          [](const Arguments& args, void* data0, void* data1) -> Local<Value> {
            auto& prop = *static_cast<typename internal::InstanceDefine::PropertyDefine*>(data0);
            auto classDefine = static_cast<const internal::ClassDefineState*>(data1);

            auto ins = verifyAndGetInstance(classDefine, args.thiz().val_);

            Tracer trace(args.engine(), prop.traceName);
            return prop.getter(ins);
          },
          &prop, classDefine);
    }

    if (prop.setter) {
      setter = Stack::newFunction(
          [](const Arguments& args, void* data0, void* data1) -> Local<Value> {
            auto& prop = *static_cast<typename internal::InstanceDefine::PropertyDefine*>(data0);
            auto classDefine = static_cast<const internal::ClassDefineState*>(data1);

            auto ins = verifyAndGetInstance(classDefine, args.thiz().val_);
            Tracer trace(args.engine(), prop.traceName);
            prop.setter(ins, args[0]);
            return {};
          },
          &prop, classDefine);
    }
    NativeHelper::defineProperty(prototype.val_, name.val_, getter, setter);
  }

  // set the `prototype` property of constructor
  obj.set("prototype", prototype);
}

}  // namespace script::wasm_backend
