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

#include <type_traits>
#include "../../src/Native.hpp"
#include "../../src/Scope.h"
#include "../../src/Utils.h"
#include "../../src/utils/Helper.hpp"
#include "JscEngine.h"
#include "JscHelper.hpp"
#include "JscReference.hpp"

namespace script::jsc_backend {

void JscEngine::performRegisterNativeClass(
    internal::TypeIndex typeIndex, const internal::ClassDefineState* classDefine,
    script::ScriptClass* (*instanceTypeToScriptClass)(void*)) {
  Tracer traceRegister(this, classDefine->className);

  Local<Value> object;
  ClassRegistryData registry{};
  registry.instanceTypeToScriptClass = instanceTypeToScriptClass;

  if (classDefine->hasInstanceDefine()) {
    defineInstance(classDefine, object, registry);
  } else {
    object = Object::newObject();
  }

  registerStaticDefine(classDefine->staticDefine, object.asObject());

  auto ns =
      ::script::internal::getNamespaceObject(this, classDefine->nameSpace, getGlobal()).asObject();
  ns.set(classDefine->className, object);

  classRegistry_.emplace(classDefine, registry);
}

void JscEngine::defineInstance(const internal::ClassDefineState* classDefine, Local<Value>& object,
                               ClassRegistryData& registry) {
  JSClassDefinition instanceDefine = kJSClassDefinitionEmpty;
  instanceDefine.attributes = kJSClassAttributeNone;
  instanceDefine.className = classDefine->className.c_str();

  instanceDefine.finalize = [](JSObjectRef thiz) {
    auto* t = static_cast<ScriptClass*>(JSObjectGetPrivate(thiz));
    auto engine = script::internal::scriptDynamicCast<JscEngine*>(t->getScriptEngine());
    if (!engine->isDestroying()) {
      utils::Message dtor([](auto& msg) {},
                          [](auto& msg) { delete static_cast<ScriptClass*>(msg.ptr0); });
      dtor.tag = engine;
      dtor.ptr0 = t;
      engine->messageQueue()->postMessage(dtor);
    } else {
      delete t;
    }
  };
  auto clazz = JSClassCreate(&instanceDefine);
  registry.instanceClass = clazz;

  JSClassDefinition staticDefine = kJSClassDefinitionEmpty;

  staticDefine.callAsConstructor = createConstructor();
  staticDefine.hasInstance = [](JSContextRef ctx, JSObjectRef constructor,
                                JSValueRef possibleInstance, JSValueRef*) -> bool {
    auto engine = static_cast<JscEngine*>(JSObjectGetPrivate(JSContextGetGlobalObject(ctx)));
    auto def = static_cast<internal::ClassDefineState*>(JSObjectGetPrivate(constructor));
    return engine->performIsInstanceOf(make<Local<Value>>(possibleInstance), def);
  };

  auto staticClass = JSClassCreate(&staticDefine);
  object = Local<Object>(
      JSObjectMake(context_, staticClass, const_cast<internal::ClassDefineState*>(classDefine)));
  // not used anymore
  JSClassRelease(staticClass);
  registry.constructor = object.asObject();

  auto prototype = defineInstancePrototype(classDefine);
  object.asObject().set("prototype", prototype);

  registry.prototype = prototype;
}

JSObjectCallAsConstructorCallback JscEngine::createConstructor() {
  return [](JSContextRef ctx, JSObjectRef constructor, size_t argumentCount,
            const JSValueRef arguments[], JSValueRef* exception) {
    auto engine = static_cast<JscEngine*>(JSObjectGetPrivate(JSContextGetGlobalObject(ctx)));
    auto def = static_cast<const internal::ClassDefineState*>(JSObjectGetPrivate(constructor));

    Tracer trace(engine, def->className);

    auto it = engine->classRegistry_.find(def);
    assert(it != engine->classRegistry_.end());

    ClassRegistryData& registry = it->second;

    auto object = JSObjectMake(ctx, registry.instanceClass, nullptr);
    auto callbackInfo = newArguments(engine, object, arguments, argumentCount);

    try {
      StackFrameScope stack;
      void* thiz;
      if (argumentCount == 2 && engine->isConstructorMarkSymbol(arguments[0]) &&
          JSValueIsObjectOfClass(engine->context_, arguments[1], externalClass_)) {
        // this logic is for
        // ScriptClass::ScriptClass(const ClassDefine<T> &define)
        auto obj = JSValueToObject(engine->context_, arguments[1], exception);
        checkException(*exception);
        thiz = JSObjectGetPrivate(obj);
      } else {
        // this logic is for
        // ScriptClass::ScriptClass(const Local<Object>& thiz)
        thiz = def->instanceDefine.constructor(callbackInfo);
      }
      if (thiz) {
        auto scriptClass = registry.instanceTypeToScriptClass(thiz);
        scriptClass->internalState_.classDefine = def;
        scriptClass->internalState_.polymorphicPointer = thiz;
        JSObjectSetPrivate(object, scriptClass);
        JSObjectSetPrototype(ctx, object,
                             toJsc(engine->context_, registry.prototype.get().asValue()));
        return object;
      } else {
        throw Exception("constructor returns null");
      }
    } catch (Exception& e) {
      *exception = toJsc(engine->context_, e.exception());
      // can't return undefined, just return empty object;
      return object;
    }
  };
}

Local<Object> JscEngine::defineInstancePrototype(const internal::ClassDefineState* classDefine) {
  Local<Object> proto = Object::newObject();

  defineInstanceFunction(classDefine, proto);

  if (!classDefine->instanceDefine.properties.empty()) {
    auto jsObject = getGlobal().get("Object").asObject();
    auto jsObject_def = jsObject.get("defineProperty").asFunction();
    auto get = String::newString("get");
    auto set = String::newString("set");

    defineInstanceProperties(classDefine, get, set, jsObject, jsObject_def, proto);
  }
  return proto;
}

void JscEngine::defineInstanceFunction(const internal::ClassDefineState* classDefine,
                                       Local<Object>& prototypeObject) {
  struct ContextData {
    typename internal::InstanceDefine::FunctionDefine* functionDefine;
    JscEngine* engine;
    const internal::ClassDefineState* classDefine;
  };

  for (auto& f : classDefine->instanceDefine.functions) {
    StackFrameScope stack;
    JSClassDefinition jsFunc = kJSClassDefinitionEmpty;
    jsFunc.className = "anonymous";
    jsFunc.callAsFunction = [](JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject,
                               size_t argumentCount, const JSValueRef arguments[],
                               JSValueRef* exception) {
      auto data = static_cast<ContextData*>(JSObjectGetPrivate(function));
      auto fp = data->functionDefine;
      auto engine = data->engine;
      auto def = data->classDefine;
      auto& callback = fp->callback;

      Tracer trace(engine, fp->traceName);

      auto args = newArguments(engine, thisObject, arguments, argumentCount);

      try {
        auto* t = static_cast<ScriptClass*>(JSObjectGetPrivate(thisObject));
        if (!t || t->internalState_.classDefine != def) {
          throw Exception(u8"call function on wrong receiver");
        }
        auto returnVal = callback(t->internalState_.polymorphicPointer, args);
        return toJsc(engine->context_, returnVal);
      } catch (Exception& e) {
        *exception = jsc_backend::JscEngine::toJsc(engine->context_, e.exception());
        return JSValueMakeUndefined(currentEngineContextChecked());
      }
    };
    jsFunc.finalize = [](JSObjectRef function) {
      delete static_cast<ContextData*>(JSObjectGetPrivate(function));
    };

    auto funcClazz = JSClassCreate(&jsFunc);
    Local<Function> funcObj(JSObjectMake(
        currentEngineContextChecked(), funcClazz,
        new ContextData{const_cast<typename internal::InstanceDefine::FunctionDefine*>(&f), this,
                        classDefine}));

    // not used anymore
    JSClassRelease(funcClazz);
    auto name = String::newString(f.name);

    prototypeObject.set(name, funcObj);
  }
}

void JscEngine::defineInstanceProperties(const internal::ClassDefineState* classDefine,
                                         const Local<String>& getString,
                                         const Local<String>& setString,
                                         const Local<Object>& jsObject,
                                         const Local<Function>& jsObject_defineProperty,
                                         const Local<Object>& prototype) {
  struct ContextData {
    typename internal::InstanceDefine::PropertyDefine* propertyDefine;
    JscEngine* engine;
    const internal::ClassDefineState* classDefine;
  };

  for (auto& p : classDefine->instanceDefine.properties) {
    StackFrameScope stack;
    Local<Value> getter;
    Local<Value> setter;

    if (p.getter) {
      JSClassDefinition jsFunc = kJSClassDefinitionEmpty;
      jsFunc.className = "getter";
      jsFunc.callAsFunction = [](JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject,
                                 size_t argumentCount, const JSValueRef arguments[],
                                 JSValueRef* exception) {
        auto data = static_cast<ContextData*>(JSObjectGetPrivate(function));
        auto pp = data->propertyDefine;
        auto engine = data->engine;
        auto def = data->classDefine;

        Tracer trace(engine, pp->traceName);

        try {
          auto* t = static_cast<ScriptClass*>(JSObjectGetPrivate(thisObject));
          if (!t || t->internalState_.classDefine != def) {
            throw Exception(u8"call function on wrong receiver");
          }
          auto value = (pp->getter)(t->internalState_.polymorphicPointer);

          return toJsc(engine->context_, value);
        } catch (Exception& e) {
          *exception = jsc_backend::JscEngine::toJsc(engine->context_, e.exception());
          return JSValueMakeUndefined(engine->context_);
        }
      };
      jsFunc.finalize = [](JSObjectRef function) {
        delete static_cast<ContextData*>(JSObjectGetPrivate(function));
      };
      auto funcClazz = JSClassCreate(&jsFunc);

      getter = Local<Function>(
          JSObjectMake(currentEngineContextChecked(), funcClazz,
                       new ContextData{const_cast<internal::InstanceDefine::PropertyDefine*>(&p),
                                       this, classDefine}));

      JSClassRelease(funcClazz);
    }

    if (p.setter) {
      JSClassDefinition jsFunc = kJSClassDefinitionEmpty;
      jsFunc.className = "setter";
      jsFunc.callAsFunction = [](JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject,
                                 size_t argumentCount, const JSValueRef arguments[],
                                 JSValueRef* exception) {
        auto data = static_cast<ContextData*>(JSObjectGetPrivate(function));
        auto pp = data->propertyDefine;
        auto engine = data->engine;
        auto def = data->classDefine;

        Tracer trace(engine, pp->traceName);

        auto args = newArguments(engine, thisObject, arguments, argumentCount);
        if (args.size() > 0) {
          try {
            auto* t = static_cast<ScriptClass*>(JSObjectGetPrivate(thisObject));
            if (!t || t->internalState_.classDefine != def) {
              throw Exception(u8"call function on wrong receiver");
            }
            (pp->setter)(t->internalState_.polymorphicPointer, args[0]);
          } catch (Exception& e) {
            *exception = jsc_backend::JscEngine::toJsc(engine->context_, e.exception());
          }
        }
        return JSValueMakeUndefined(engine->context_);
      };
      jsFunc.finalize = [](JSObjectRef function) {
        delete static_cast<ContextData*>(JSObjectGetPrivate(function));
      };
      auto funcClazz = JSClassCreate(&jsFunc);

      setter = Local<Function>(JSObjectMake(
          context_, funcClazz,
          new ContextData{const_cast<typename internal::InstanceDefine::PropertyDefine*>(&p), this,
                          classDefine}));

      JSClassRelease(funcClazz);
    }

    auto desc = Object::newObject();
    if (!getter.isNull()) desc.set(getString, getter);
    if (!setter.isNull()) desc.set(setString, setter);

    // set prop to prototype
    jsObject_defineProperty.call(
        jsObject, std::initializer_list<Local<Value>>{prototype, String::newString(p.name), desc});
  }
}

Local<Object> JscEngine::performNewNativeClass(internal::TypeIndex typeIndex,
                                               const internal::ClassDefineState* classDefine,
                                               size_t size, const Local<script::Value>* args) {
  auto it = classRegistry_.find(const_cast<internal::ClassDefineState*>(classDefine));
  if (it == classRegistry_.end()) {
    throw Exception("class define[" + classDefine->className + "] is not registered");
  }

  if (it != classRegistry_.end() && !it->second.constructor.isEmpty()) {
    return jsc_backend::toJscValues<Local<Object>>(
        context_, size, args, [this, &it, size](JSValueRef* array) {
          JSValueRef jscException = nullptr;
          Local<Value> ret(JSObjectCallAsConstructor(
              context_, toJsc(context_, it->second.constructor.get()), size, array, &jscException));
          jsc_backend::JscEngine::checkException(jscException);
          return ret.asObject();
        });
  }

  throw Exception("can't create native class");
}

bool JscEngine::performIsInstanceOf(const Local<script::Value>& value,
                                    const internal::ClassDefineState* classDefine) {
  if (!value.isObject()) return false;
  auto it = classRegistry_.find(const_cast<internal::ClassDefineState*>(classDefine));

  if (it != classRegistry_.end() && !it->second.constructor.isEmpty()) {
    return JSValueIsObjectOfClass(context_, toJsc(context_, value), it->second.instanceClass);
  }

  return false;
}

void* JscEngine::performGetNativeInstance(const Local<script::Value>& value,
                                          const internal::ClassDefineState* classDefine) {
  if (value.isObject() && performIsInstanceOf(value, classDefine)) {
    return static_cast<ScriptClass*>(JSObjectGetPrivate(value.asObject().val_))
        ->internalState_.polymorphicPointer;
  }
  return nullptr;
}

}  // namespace script::jsc_backend
