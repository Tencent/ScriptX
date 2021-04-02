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

#include "QjsEngine.h"
#include <ScriptX/ScriptX.h>

namespace script::qjs_backend {

JSClassID QjsEngine::kPointerClassId = 0;
JSClassID QjsEngine::kInstanceClassId = 0;
JSClassID QjsEngine::kFunctionDataClassId = 0;
static std::once_flag kGlobalQjsClass;

constexpr auto kGetByteBufferInfo = R"(
(function (val) {
  // NOTE: KEEP SYNC WITH CPP
  const kUnspecified = 0x1;
  const kInt8 = 0x101;
  const kUint8 = 0x201;
  const kInt16 = 0x302;
  const kUint16 = 0x402;
  const kInt32 = 0x504;
  const kUint32 = 0x604;
  const kInt64 = 0x708;
  const kUint64 = 0x808;
  const KFloat32 = 0x904;
  const kFloat64 = 0xa08;

  let byteBuffer = val;
  let length = val.byteLength;
  let offset = 0;
  let type = kUnspecified;

  if (ArrayBuffer.isView(val)) {
    byteBuffer = val.buffer;
    offset = val.byteOffset;

    if (val instanceof Int8Array) {
      type = kInt8;
    } else if (val instanceof Uint8Array || val instanceof Uint8ClampedArray) {
      type = kUint8;
    } else if (val instanceof Int16Array) {
      type = kInt16;
    } else if (val instanceof Uint16Array) {
      type = kUint16;
    } else if (val instanceof Int16Array) {
      type = kUint16;
    } else if (val instanceof Int32Array) {
      type = kInt32;
    } else if (val instanceof Uint32Array) {
      type = kUint32;
    } else if (val instanceof Float32Array) {
      type = KFloat32;
    } else if (val instanceof Float64Array) {
      type = kFloat64;
    } else if (val instanceof BigInt64Array) {
      type = kInt64;
    } else if (val instanceof BigUint64Array) {
      type = kUint64;
    }
  }

  return [byteBuffer, length, offset, type];
})
)";

QjsEngine::QjsEngine(std::shared_ptr<utils::MessageQueue> queue, const QjsFactory& factory)
    : queue_(queue ? std::move(queue) : std::make_shared<utils::MessageQueue>()) {
  std::call_once(kGlobalQjsClass, []() {
    JS_NewClassID(&kPointerClassId);
    JS_NewClassID(&kInstanceClassId);
    JS_NewClassID(&kFunctionDataClassId);
  });

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

  JSClassDef instance{};
  instance.class_name = "ScriptXInstance";
  instance.finalizer = [](JSRuntime* /*rt*/, JSValue val) {
    auto ptr = JS_GetOpaque(val, kInstanceClassId);
    if (ptr) {
      auto opaque = static_cast<InstanceClassOpaque*>(ptr);
      // reset the weak reference
      opaque->scriptClassPointer->internalState_.weakRef_ = JS_UNDEFINED;
      delete opaque->scriptClassPointer;
      delete opaque;
    }
  };
  JS_NewClass(runtime_, kInstanceClassId, &instance);

  lengthAtom_ = JS_NewAtom(context_, "length");

  {
    EngineScope scope(this);
    {
      auto ret = static_cast<ScriptEngine*>(this)->eval("(function(a, b) {return a === b;})");
      helperFunctionStrictEqual_ = qjs_interop::getLocal(ret);
    }

    {
      auto ret = static_cast<ScriptEngine*>(this)->eval(
          "(function(b) { return b instanceof ArrayBuffer || b instanceof SharedArrayBuffer || "
          "ArrayBuffer.isView(b);})");
      helperFunctionIsByteBuffer_ = qjs_interop::getLocal(ret);
    }

    {
      auto ret = static_cast<ScriptEngine*>(this)->eval(kGetByteBufferInfo);
      helperFunctionGetByteBufferInfo_ = qjs_interop::getLocal(ret);
    }
  }
}

QjsEngine::~QjsEngine() = default;

void QjsEngine::destroy() noexcept {
  ScriptEngine::destroyUserData();

  queue_->removeMessageByTag(static_cast<ScriptEngine*>(this));

  JS_FreeAtom(context_, lengthAtom_);
  JS_FreeValue(context_, helperFunctionStrictEqual_);
  JS_FreeValue(context_, helperFunctionIsByteBuffer_);
  JS_FreeValue(context_, helperFunctionGetByteBufferInfo_);

  for (auto&& [key, v] : nativeInstanceRegistry_) {
    JS_FreeValue(context_, v.first);
    JS_FreeValue(context_, v.second);
  }
  nativeInstanceRegistry_.clear();

  JS_RunGC(runtime_);
  JS_FreeContext(context_);
  JS_FreeRuntime(runtime_);
}

Local<Value> QjsEngine::get(const Local<String>& key) { return getGlobal().get(key); }

void QjsEngine::set(const Local<String>& key, const Local<Value>& value) {
  getGlobal().set(key, value);
}

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
  Tracer trace(this, "QjsEngine::eval");
  JSValue ret = JS_UNDEFINED;
  StringHolder sh(script);

  if (sourceFile.isString()) {
    StringHolder source(sourceFile.asString());
    ret = JS_Eval(context_, sh.c_str(), sh.length(), source.c_str(), JS_EVAL_TYPE_GLOBAL);
  } else {
    ret = JS_Eval(context_, sh.c_str(), sh.length(), "<unknown>", JS_EVAL_TYPE_GLOBAL);
  }
  qjs_backend::checkException(ret);

  return Local<Value>(ret);
}

std::shared_ptr<utils::MessageQueue> QjsEngine::messageQueue() { return queue_; }

void QjsEngine::gc() { JS_RunGC(runtime_); }

size_t QjsEngine::getHeapSize() {
  JSMemoryUsage usage{};
  JS_ComputeMemoryUsage(runtime_, &usage);
  return usage.memory_used_size;
}

void QjsEngine::adjustAssociatedMemory(int64_t count) {}

ScriptLanguage QjsEngine::getLanguageType() { return ScriptLanguage::kJavaScript; }

std::string QjsEngine::getEngineVersion() { return "QuickJS"; }

bool QjsEngine::isDestroying() const { return false; }

void QjsEngine::registerNativeStatic(const Local<Object>& module,
                                     const internal::StaticDefine& def) {
  for (auto&& f : def.functions) {
    auto ptr = &f.callback;

    auto fun = newRawFunction(context_, const_cast<FunctionCallback*>(ptr), nullptr,
                              [](const Arguments& args, void* data1, void*, bool) {
                                return (*static_cast<FunctionCallback*>(data1))(args);
                              });
    module.set(f.name, fun);
  }

  for (auto&& prop : def.properties) {
    Local<Value> getterFun;
    Local<Value> setterFun;
    if (prop.getter) {
      getterFun = newRawFunction(context_, const_cast<GetterCallback*>(&prop.getter), nullptr,
                                 [](const Arguments& args, void* data, void*, bool) {
                                   return (*static_cast<GetterCallback*>(data))();
                                 })
                      .asValue();
    }

    if (prop.setter) {
      setterFun = newRawFunction(context_, const_cast<SetterCallback*>(&prop.setter), nullptr,
                                 [](const Arguments& args, void* data, void*, bool) {
                                   (*static_cast<SetterCallback*>(data))(args[0]);
                                   return Local<Value>();
                                 });
    }

    auto atom = JS_NewAtomLen(context_, prop.name.c_str(), prop.name.length());

    // TODO: flags
    auto ret = JS_DefinePropertyGetSet(context_, qjs_interop::peekLocal(module), atom,
                                       qjs_interop::getLocal(getterFun),
                                       qjs_interop::getLocal(setterFun), 0);
    JS_FreeAtom(context_, atom);
    qjs_backend::checkException(ret);
  }
}

Local<Object> QjsEngine::getNamespaceForRegister(const std::string_view& nameSpace) {
  Local<Object> nameSpaceObj = getGlobal();
  if (!nameSpace.empty()) {
    std::size_t begin = 0;
    while (begin < nameSpace.size()) {
      auto index = nameSpace.find('.', begin);
      if (index == std::string::npos) {
        index = nameSpace.size();
      }
      auto key = String::newString(nameSpace.substr(begin, index - begin));
      auto obj = nameSpaceObj.get(key);
      if (obj.isNull()) {
        // new plain object
        obj = Object::newObject();
        nameSpaceObj.set(key, obj);
      } else if (!obj.isObject()) {
        throw Exception("invalid namespace");
      }

      nameSpaceObj = obj.asObject();
      begin = index + 1;
    }
  }
  return nameSpaceObj;
}

}  // namespace script::qjs_backend
