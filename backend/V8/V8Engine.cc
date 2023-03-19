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

#include "V8Engine.h"
#include <cassert>
#include <memory>
#include "V8Helper.hpp"
#include "V8Native.hpp"
#include "V8Reference.hpp"

namespace script::v8_backend {

// create a master engine (opposite to slave engine)
V8Engine::V8Engine(std::shared_ptr<utils::MessageQueue> mq) : V8Engine(std::move(mq), nullptr) {}

V8Engine::V8Engine(std::shared_ptr<utils::MessageQueue> mq,
                   const std::function<v8::Isolate*()>& isolateFactory)
    : v8Platform_(V8Platform::getPlatform()),
      messageQueue_(mq ? std::move(mq) : std::make_shared<utils::MessageQueue>()) {
  // init v8
  v8::V8::Initialize();

  // create isolation
  if (isolateFactory) {
    isolate_ = isolateFactory();
  } else {
    v8::Isolate::CreateParams createParams;
    allocator_.reset(v8::ArrayBuffer::Allocator::NewDefaultAllocator());
    createParams.array_buffer_allocator = allocator_.get();
    isolate_ = v8::Isolate::New(createParams);
  }
  v8Platform_->addEngineInstance(isolate_, this);

  isolate_->SetCaptureStackTraceForUncaughtExceptions(true);

  initContext();
}

V8Engine::V8Engine(std::shared_ptr<utils::MessageQueue> messageQueue, v8::Isolate* isolate,
                   v8::Local<v8::Context> context, bool addGlobalEngineScope)
    : isOwnIsolate_(false),
      v8Platform_(),
      messageQueue_(messageQueue ? std::move(messageQueue)
                                 : std::make_shared<utils::MessageQueue>()),
      isolate_(isolate) {
  context_ = v8::Global<v8::Context>(isolate, context);
  initContext();

  auto currentScope = EngineScope::getCurrent();
  if (currentScope) {
    throw std::logic_error("create V8Engine with an existing EngineScope");
  }
  if (addGlobalEngineScope) {
    threadGlobalScope_ = std::make_unique<ThreadGlobalScope>(this);
  }
}

void V8Engine::initContext() {
  v8::Locker locker(isolate_);
  v8::Isolate::Scope is(isolate_);
  v8::HandleScope handle_scope(isolate_);
  if (context_.IsEmpty()) {
    auto context = v8::Context::New(isolate_);
    context_ = v8::Global<v8::Context>(isolate_, context);
  }
  internalStoreSymbol_ = v8::Global<v8::Symbol>(isolate_, v8::Symbol::New(isolate_));
  constructorMarkSymbol_ = v8::Global<v8::Symbol>(isolate_, v8::Symbol::New(isolate_));
}

V8Engine::~V8Engine() = default;

// NOLINTNEXTLINE(bugprone-exception-escape)
void V8Engine::destroy() noexcept {
  ScriptEngine::destroyUserData();
  {
    EngineScope scope(this);
    isDestroying_ = true;

    // Isolate::Dispose don't do gc.
    // (For performance reason, it just tear down the heap).
    // we must manually release native object explicitly
    for (auto& it : managedObject_) {
      // reset weak first
      it.second.Reset();
      // do destruct
      auto data = it.first;
      data->cleanupFunc(data->data);
      delete data;
    }
    managedObject_.clear();
    keptObject_.clear();

    nativeRegistry_.clear();
    globalWeakBookkeeping_.clear();

    internalStoreSymbol_.Reset();
    constructorMarkSymbol_.Reset();
    context_.Reset();
  }
  messageQueue_->removeMessageByTag(this);
  messageQueue_.reset();

  if (isOwnIsolate_) {
    isolate_->Dispose();
    v8Platform_->removeEngineInstance(isolate_);
  }
  delete this;
}

bool V8Engine::isDestroying() const { return isDestroying_; }

// create a slave engine from master
V8Engine::V8Engine(V8Engine* masterEngine)
    : isOwnIsolate_(false),
      messageQueue_(masterEngine->messageQueue()),
      isolate_(masterEngine->isolate_) {
  initContext();
}

UniqueEnginePtr V8Engine::newSlaveEngine() { return UniqueEnginePtr(new V8Engine(this)); }

std::shared_ptr<script::utils::MessageQueue> V8Engine::messageQueue() { return messageQueue_; }

ScriptLanguage V8Engine::getLanguageType() { return ScriptLanguage::kJavaScript; }

std::string V8Engine::getEngineVersion() { return std::string("V8 ") + v8::V8::GetVersion(); }

Local<Object> V8Engine::getGlobal() {
  return Local<Value>(context_.Get(isolate_)->Global()).asObject();
}

Local<Value> V8Engine::get(const script::Local<script::String>& key) {
  return getGlobal().get(key);
}

void V8Engine::set(const script::Local<script::String>& key,
                   const script::Local<script::Value>& value) {
  getGlobal().set(key, value);
}

Local<Value> V8Engine::eval(const Local<String>& script, const Local<Value>& sourceFile) {
  Tracer trace(this, "V8Engine::eval");
  v8::TryCatch tryCatch(isolate_);
  auto context = context_.Get(isolate_);
  v8::Local<v8::String> scriptString = toV8(isolate_, script);
  if (scriptString.IsEmpty() || scriptString->IsNullOrUndefined()) {
    throw Exception("can't eval script");
  }
  v8::ScriptOrigin origin(
#if SCRIPTX_V8_VERSION_AT_LEAST(9, 0)
      // V8 9.0 add isolate param for external API
      isolate_,
#endif
      sourceFile.isNull() || !sourceFile.isString() ? v8::Local<v8::String>()
                                                    : toV8(isolate_, sourceFile.asString()));
  v8::MaybeLocal<v8::Script> maybeScript = v8::Script::Compile(context, scriptString, &origin);
  v8_backend::checkException(tryCatch);
  auto maybeResult = maybeScript.ToLocalChecked()->Run(context);
  v8_backend::checkException(tryCatch);
  return make<Local<Value>>(maybeResult.ToLocalChecked());
}

Local<Value> V8Engine::eval(const Local<String>& script, const Local<String>& sourceFile) {
  return eval(script, sourceFile.asValue());
}

Local<Value> V8Engine::eval(const Local<String>& script) { return eval(script, {}); }

void V8Engine::registerNativeClassStatic(v8::Local<v8::FunctionTemplate> funcT,
                                         const internal::StaticDefine* staticDefine) {
  for (auto& prop : staticDefine->properties) {
    StackFrameScope stack;
    auto name = String::newString(prop.name);

    v8::AccessorGetterCallback getter = nullptr;
    v8::AccessorSetterCallback setter = nullptr;

    if (prop.getter) {
      getter = [](v8::Local<v8::String> /*property*/,
                  const v8::PropertyCallbackInfo<v8::Value>& info) {
        auto ptr = static_cast<internal::StaticDefine::PropertyDefine*>(
            info.Data().As<v8::External>()->Value());
        Tracer trace(EngineScope::currentEngine(), ptr->traceName);
        Local<Value> ret = ptr->getter();
        try {
          info.GetReturnValue().Set(toV8(info.GetIsolate(), ret));
        } catch (const Exception& e) {
          v8_backend::rethrowException(e);
        }
      };
    }

    if (prop.setter) {
      setter = [](v8::Local<v8::String> /*property*/, v8::Local<v8::Value> value,
                  const v8::PropertyCallbackInfo<void>& info) {
        auto ptr = static_cast<internal::StaticDefine::PropertyDefine*>(
            info.Data().As<v8::External>()->Value());
        Tracer trace(EngineScope::currentEngine(), ptr->traceName);
        try {
          ptr->setter(make<Local<Value>>(value));
        } catch (const Exception& e) {
          v8_backend::rethrowException(e);
        }
      };
    } else {
      // v8 requires setter to be present, otherwise, a real js set code with create a new
      // property...
      setter = [](v8::Local<v8::String> property, v8::Local<v8::Value> value,
                  const v8::PropertyCallbackInfo<void>& info) {};
    }

    funcT->SetNativeDataProperty(
        toV8(isolate_, name), getter, setter,
        v8::External::New(isolate_, const_cast<internal::StaticDefine::PropertyDefine*>(&prop)),
        v8::PropertyAttribute::DontDelete);
  }

  for (auto& func : staticDefine->functions) {
    StackFrameScope stack;
    auto name = String::newString(func.name);

    auto fn = v8::FunctionTemplate::New(
        isolate_,
        [](const v8::FunctionCallbackInfo<v8::Value>& info) {
          auto funcDef = reinterpret_cast<internal::StaticDefine::FunctionDefine*>(
              info.Data().As<v8::External>()->Value());
          auto engine = v8_backend::currentEngine();
          Tracer trace(engine, funcDef->traceName);

          try {
            auto returnVal = (funcDef->callback)(extractV8Arguments(engine, info));
            info.GetReturnValue().Set(v8_backend::V8Engine::toV8(info.GetIsolate(), returnVal));
          } catch (Exception& e) {
            v8_backend::rethrowException(e);
          }
        },
        v8::External::New(isolate_, const_cast<internal::StaticDefine::FunctionDefine*>(&func)), {},
        0, v8::ConstructorBehavior::kThrow);
    if (!fn.IsEmpty()) {
      funcT->Set(toV8(isolate_, name), fn, v8::PropertyAttribute::DontDelete);
    } else {
      throw Exception("can't create function for static");
    }
  }
}

void V8Engine::gc() {
  if (isDestroying()) return;
  EngineScope engineScope(this);
  isolate_->LowMemoryNotification();
}

size_t V8Engine::getHeapSize() {
  EngineScope engineScope(this);
  v8::HeapStatistics heapStatistics;
  isolate_->GetHeapStatistics(&heapStatistics);
  return heapStatistics.used_heap_size() + heapStatistics.malloced_memory() +
         heapStatistics.external_memory();
}

void V8Engine::adjustAssociatedMemory(int64_t count) {
  if (isDestroying()) return;
  EngineScope engineScope(this);
  isolate_->AdjustAmountOfExternalAllocatedMemory(count);
}

void V8Engine::addManagedObject(void* nativeObj, v8::Local<v8::Value> obj,
                                std::function<void(void*)>&& proc) {
  auto data = std::make_unique<ManagedObject>();
  data->engine = this;
  data->data = nativeObj;
  data->cleanupFunc = std::move(proc);
  v8::Global<v8::Value> weak(isolate_, obj);

  weak.SetWeak(
      static_cast<void*>(data.get()),
      [](const v8::WeakCallbackInfo<void>& info) {
        auto param = static_cast<ManagedObject*>(info.GetParameter());
        auto engine = param->engine;
        {
          v8::Locker lk(engine->isolate_);
          auto it = engine->managedObject_.find(param);
          assert(it != engine->managedObject_.end());
          engine->managedObject_.erase(it);

          info.SetSecondPassCallback([](const v8::WeakCallbackInfo<void>& data) {
            auto param = static_cast<ManagedObject*>(data.GetParameter());
            auto engine = param->engine;
            v8::Locker lk(engine->isolate_);

            param->cleanupFunc(param->data);
            delete param;
          });
        }
      },
      v8::WeakCallbackType::kParameter);

  managedObject_.emplace(data.release(), std::move(weak));
}

size_t V8Engine::keepReference(const Local<Value>& ref) {
  auto id = keptObjectId_++;
  keptObject_.emplace(id, v8::Global<v8::Value>{isolate_, toV8(isolate_, ref)});
  return id;
}

void V8Engine::removeKeptReference(size_t id) {
  EngineScope scope(this);
  keptObject_.erase(id);
}

// Native

constexpr int kInstanceObjectAlignedPointer_ScriptClass = 0;         // ScriptClass* pointer
constexpr int kInstanceObjectAlignedPointer_PolymorphicPointer = 0;  // the actual type pointer

void V8Engine::performRegisterNativeClass(
    internal::TypeIndex typeIndex, const internal::ClassDefineState* classDefine,
    script::ScriptClass* (*instanceTypeToScriptClass)(void*)) {
  StackFrameScope stack;
  v8::TryCatch tryCatch(isolate_);

  Local<Object> nameSpaceObj =
      ::script::internal::getNamespaceObject(this, classDefine->nameSpace, getGlobal()).asObject();

  v8::Local<v8::FunctionTemplate> funcT;

  if (classDefine->hasInstanceDefine()) {
    funcT = newConstructor(classDefine, instanceTypeToScriptClass);
  } else {
    funcT =
        v8::FunctionTemplate::New(isolate_, nullptr, {}, {}, 0, v8::ConstructorBehavior::kThrow);
    funcT->RemovePrototype();
  }

  auto className = String::newString(classDefine->className);
  funcT->SetClassName(v8_backend::V8Engine::toV8(isolate_, className));

  registerNativeClassStatic(funcT, &classDefine->staticDefine);
  registerNativeClassInstance(funcT, classDefine);

  auto function = funcT->GetFunction(v8_backend::currentEngineContextChecked());
  v8_backend::checkException(tryCatch);

  auto global = v8::Global<v8::FunctionTemplate>(isolate_, funcT);
  nativeRegistry_.emplace(classDefine, std::move(global));

  nameSpaceObj.set(className, make<Local<Function>>(function.ToLocalChecked()));
}

v8::Local<v8::FunctionTemplate> V8Engine::newConstructor(
    const internal::ClassDefineState* classDefine,
    script::ScriptClass* (*instanceTypeToScriptClass)(void*)) {
  v8::TryCatch tryCatch(isolate_);

  v8::Local<v8::Object> data = v8::Object::New(isolate_);
  checkException(tryCatch);
  auto context = context_.Get(isolate_);
  auto ret =
      data->Set(context, 0,
                v8::External::New(isolate_, const_cast<internal::ClassDefineState*>(classDefine)));
  (void)ret;
  checkException(tryCatch);

  ret = data->Set(context, 1, v8::External::New(isolate_, this));
  (void)ret;
  checkException(tryCatch);

  ret = data->Set(context, 2,
                  v8::External::New(isolate_, reinterpret_cast<void*>(instanceTypeToScriptClass)));
  (void)ret;
  checkException(tryCatch);

  auto funcT = v8::FunctionTemplate::New(
      isolate_,
      [](const v8::FunctionCallbackInfo<v8::Value>& args) {
        auto context = args.GetIsolate()->GetCurrentContext();
        v8::Local<v8::Object> data = args.Data().As<v8::Object>();
        auto classDefine = reinterpret_cast<internal::ClassDefineState*>(
            data->Get(context, 0).ToLocalChecked().As<v8::External>()->Value());
        auto engine = reinterpret_cast<V8Engine*>(
            data->Get(context, 1).ToLocalChecked().As<v8::External>()->Value());
        auto instanceTypeToScriptClass = reinterpret_cast<script::ScriptClass* (*)(void*)>(
            data->Get(context, 2).ToLocalChecked().As<v8::External>()->Value());
        auto& constructor = classDefine->instanceDefine.constructor;

        Tracer trace(engine, classDefine->className.c_str());
        try {
          StackFrameScope stack;
          if (!args.IsConstructCall()) {
            throw Exception(u8"constructor can't be called as function");
          }
          void* ret;
          if (args.Length() == 2 && args[0]->IsSymbol() &&
              args[0]->StrictEquals(engine->constructorMarkSymbol_.Get(args.GetIsolate())) &&
              args[1]->IsExternal()) {
            // this logic is for
            // ScriptClass::ScriptClass(ConstructFromCpp<T>)
            ret = args[1].As<v8::External>()->Value();
          } else {
            // this logic is for
            // ScriptClass::ScriptClass(const Local<Object>& thiz)
            ret = constructor(extractV8Arguments(engine, args));
          }

          if (ret != nullptr) {
            ScriptClass* scriptClass = instanceTypeToScriptClass(ret);
            scriptClass->internalState_.classDefine_ = static_cast<void*>(classDefine);

            args.This()->SetAlignedPointerInInternalField(kInstanceObjectAlignedPointer_ScriptClass,
                                                          scriptClass);
            args.This()->SetAlignedPointerInInternalField(
                kInstanceObjectAlignedPointer_PolymorphicPointer, ret);
            engine->adjustAssociatedMemory(
                static_cast<int64_t>(classDefine->instanceDefine.instanceSize));

            engine->addManagedObject(scriptClass, args.This(), [](void* ptr) {
              auto scriptClass = static_cast<ScriptClass*>(ptr);
              auto engine = scriptClass->internalState_.scriptEngine_;
              engine->adjustAssociatedMemory(-static_cast<int64_t>(
                  static_cast<internal::ClassDefineState*>(scriptClass->internalState_.classDefine_)
                      ->instanceDefine.instanceSize));
              delete scriptClass;
            });

          } else {
            throw Exception("can't create class " + classDefine->className);
          }
        } catch (Exception& e) {
          v8_backend::rethrowException(e);
        }
      },
      data);
  funcT->InstanceTemplate()->SetInternalFieldCount(1);
  return funcT;
}

void V8Engine::registerNativeClassInstance(v8::Local<v8::FunctionTemplate> funcT,
                                           const internal::ClassDefineState* classDefine) {
  if (!classDefine->instanceDefine.constructor) return;

  // instance
  auto instanceT = funcT->PrototypeTemplate();
  auto signature = v8::Signature::New(isolate_, funcT);
  for (auto& prop : classDefine->instanceDefine.properties) {
    StackFrameScope stack;
    auto name = String::newString(prop.name);

    v8::AccessorGetterCallback getter = nullptr;
    v8::AccessorSetterCallback setter = nullptr;

    if (prop.getter) {
      getter = [](v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
        auto ptr = static_cast<decltype(&prop)>(info.Data().As<v8::External>()->Value());
        auto thiz = static_cast<void*>(info.This()->GetAlignedPointerFromInternalField(
            kInstanceObjectAlignedPointer_PolymorphicPointer));
        auto scriptClass =
            static_cast<ScriptClass*>(info.This()->GetAlignedPointerFromInternalField(
                kInstanceObjectAlignedPointer_ScriptClass));
        auto& getter = ptr->getter;

        Tracer trace(scriptClass->getScriptEngine(), ptr->traceName);

        Local<Value> ret = (getter)(thiz);
        try {
          info.GetReturnValue().Set(toV8(info.GetIsolate(), ret));
        } catch (const Exception& e) {
          v8_backend::rethrowException(e);
        }
      };
    }

    if (prop.setter) {
      setter = [](v8::Local<v8::String> property, v8::Local<v8::Value> value,
                  const v8::PropertyCallbackInfo<void>& info) {
        auto ptr = static_cast<decltype(&prop)>(info.Data().As<v8::External>()->Value());
        auto thiz = static_cast<void*>(info.This()->GetAlignedPointerFromInternalField(
            kInstanceObjectAlignedPointer_PolymorphicPointer));
        auto scriptClass =
            static_cast<ScriptClass*>(info.This()->GetAlignedPointerFromInternalField(
                kInstanceObjectAlignedPointer_ScriptClass));
        auto& setter = ptr->setter;

        Tracer trace(scriptClass->getScriptEngine(), ptr->traceName);

        try {
          (setter)(thiz, make<Local<Value>>(value));
        } catch (const Exception& e) {
          v8_backend::rethrowException(e);
        }
      };
    }

    auto v8Name = toV8(isolate_, name);
    auto data = v8::External::New(
        isolate_, const_cast<typename internal::InstanceDefine::PropertyDefine*>(&prop));

#if SCRIPTX_V8_VERSION_AT_MOST(10, 1)  // SetAccessor AccessorSignature deprecated in 10.2 a8beac
    auto accessSignature = v8::AccessorSignature::New(isolate_, funcT);
    instanceT->SetAccessor(v8Name, getter, setter, data, v8::AccessControl::DEFAULT,
                           v8::PropertyAttribute::DontDelete, accessSignature);
#else
    instanceT->SetAccessor(v8Name, getter, setter, data, v8::AccessControl::DEFAULT,
                           v8::PropertyAttribute::DontDelete);
#endif
  }

  for (auto& func : classDefine->instanceDefine.functions) {
    StackFrameScope stack;
    auto name = String::newString(func.name);
    using FuncDefPtr = typename internal::InstanceDefine::FunctionDefine*;
    auto fn = v8::FunctionTemplate::New(
        isolate_,
        [](const v8::FunctionCallbackInfo<v8::Value>& info) {
          auto ptr = static_cast<FuncDefPtr>(info.Data().As<v8::External>()->Value());
          auto thiz = static_cast<void*>(info.This()->GetAlignedPointerFromInternalField(
              kInstanceObjectAlignedPointer_PolymorphicPointer));
          auto scriptClass =
              static_cast<ScriptClass*>(info.This()->GetAlignedPointerFromInternalField(
                  kInstanceObjectAlignedPointer_ScriptClass));
          auto engine = scriptClass->getScriptEngineAs<V8Engine>();

          Tracer trace(engine, ptr->traceName);
          try {
            auto returnVal = (ptr->callback)(thiz, extractV8Arguments(engine, info));
            info.GetReturnValue().Set(v8_backend::V8Engine::toV8(info.GetIsolate(), returnVal));
          } catch (Exception& e) {
            v8_backend::rethrowException(e);
          }
        },
        v8::External::New(isolate_, const_cast<FuncDefPtr>(&func)), signature);
    if (!fn.IsEmpty()) {
      funcT->PrototypeTemplate()->Set(toV8(isolate_, name), fn, v8::PropertyAttribute::DontDelete);
    } else {
      throw Exception("can't create function for instance");
    }
  }
}

Local<Object> V8Engine::performNewNativeClass(internal::TypeIndex typeIndex,
                                              const internal::ClassDefineState* classDefine,
                                              size_t size, const Local<script::Value>* args) {
  auto it = nativeRegistry_.find(classDefine);
  if (it == nativeRegistry_.end()) {
    throw Exception("class define[" + classDefine->className + "] is not registered");
  }

  auto context = context_.Get(isolate_);
  v8::TryCatch tryCatch(isolate_);
  auto funcT = it->second.Get(isolate_);
  auto function = funcT->GetFunction(context);
  v8_backend::checkException(tryCatch);

  auto ret = toV8ValueArray<v8::MaybeLocal<v8::Object>>(
      isolate_, size, args, [size, &function, &context](auto* args) {
        return function.ToLocalChecked()->NewInstance(context, static_cast<int>(size), args);
      });
  v8_backend::checkException(tryCatch);
  return Local<Object>(ret.ToLocalChecked());
}

bool V8Engine::performIsInstanceOf(const Local<script::Value>& value,
                                   const internal::ClassDefineState* classDefine) {
  auto it = nativeRegistry_.find(classDefine);
  if (it != nativeRegistry_.end()) {
    auto funcT = it->second.Get(isolate_);
    return funcT->HasInstance(toV8(isolate_, value));
  }
  return false;
}

void* V8Engine::performGetNativeInstance(const Local<script::Value>& value,
                                         const internal::ClassDefineState* classDefine) {
  if (performIsInstanceOf(value, classDefine)) {
    auto obj = toV8(isolate_, value).As<v8::Object>();
    return static_cast<void*>(
        obj->GetAlignedPointerFromInternalField(kInstanceObjectAlignedPointer_PolymorphicPointer));
  }
  return nullptr;
}

}  // namespace script::v8_backend
