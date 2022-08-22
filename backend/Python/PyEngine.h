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

#include "../../src/Engine.h"
#include "../../src/Exception.h"
#include "../../src/utils/MessageQueue.h"
#include "PyHelper.hpp"
#include <stack>

namespace script::py_backend {

class PyTssStorage;

// an PyEngine = a subinterpreter
class PyEngine : public ScriptEngine {
 private:
  std::shared_ptr<::script::utils::MessageQueue> queue_;
  
  static PyThreadState* mainThreadState;    // Global thread state of main interpreter
  PyInterpreterState* subInterpreterState;
  PyTssStorage subThreadState;      // Sub thread state of this sub interpreter (in TLS)

  // When you use EngineScope to enter a new engine(subinterpreter)
  // and find that there is an existing thread state owned by another engine,
  // we need to push its thread state to stack and release GIL to avoid dead-lock
  // -- see more code in "PyScope.cc"
  std::stack<PyThreadState*> oldThreadStateStack;
  
  friend class PyEngineScopeImpl;
  friend class PyExitEngineScopeImpl;

 public:
  PyEngine(std::shared_ptr<::script::utils::MessageQueue> queue);

  PyEngine();

  SCRIPTX_DISALLOW_COPY_AND_MOVE(PyEngine);

  void destroy() noexcept override;

  bool isDestroying() const override;

  Local<Value> get(const Local<String>& key) override;

  void set(const Local<String>& key, const Local<Value>& value) override;
  using ScriptEngine::set;

  Local<Value> eval(const Local<String>& script, const Local<Value>& sourceFile);
  Local<Value> eval(const Local<String>& script, const Local<String>& sourceFile) override;
  Local<Value> eval(const Local<String>& script) override;
  using ScriptEngine::eval;

  Local<Value> loadFile(const Local<String>& scriptFile) override;

  std::shared_ptr<utils::MessageQueue> messageQueue() override;

  void gc() override;

  void adjustAssociatedMemory(int64_t count) override;

  ScriptLanguage getLanguageType() override;

  std::string getEngineVersion() override;

 protected:
  ~PyEngine() override;

 private:
  template <typename T>
  void registerNativeClassImpl(const ClassDefine<T>* classDefine) {
    struct ScriptXHeapTypeObject {
      PyObject_HEAD;
      const ClassDefine<T>* classDefine;
      T* instance;
    };
    PyType_Slot slots[] = {
        {Py_tp_new, nullptr},
        {Py_tp_dealloc, static_cast<destructor>([](PyObject* self) {
           ScriptXHeapTypeObject* thiz = reinterpret_cast<ScriptXHeapTypeObject*>(self);
           delete thiz->instance;
           Py_TYPE(self)->tp_free(self);
         })},
        {Py_tp_init,
         static_cast<initproc>([](PyObject* self, PyObject* args, PyObject* kwds) -> int {
           if (kwds) {
             PyErr_SetString(PyExc_TypeError, "Constructor doesn't support keyword arguments");
             return -1;
           }
           PyEngine* engine = EngineScope::currentEngineAs<PyEngine>();
           ScriptXHeapTypeObject* thiz = reinterpret_cast<ScriptXHeapTypeObject*>(self);
           if (thiz->classDefine->instanceDefine.constructor) {
             thiz->instance = thiz->classDefine->instanceDefine.constructor(
                 py_interop::makeArguments(engine, self, args));
           }
           return 0;
         })},
        {0, nullptr},
    };
    PyType_Spec spec{classDefine->className.c_str(), sizeof(ScriptXHeapTypeObject), 0,
                     Py_TPFLAGS_HEAPTYPE, slots};
    PyObject* type = PyType_FromSpec(&spec);
    if (type == nullptr) {
      checkException();
      throw Exception("Failed to create type for class " + classDefine->className);
    }
    nativeDefineRegistry_.emplace(classDefine, Global<Value>(Local<Value>(type)));
    set(String::newString(classDefine->className.c_str()), Local<Value>(type));
  }

  template <>
  void registerNativeClassImpl(const ClassDefine<void>* classDefine) {
    struct ScriptXHeapTypeObject {
      PyObject_HEAD;
      const ClassDefine<void>* classDefine;
      void* instance;
    };

    PyType_Slot slots[] = {
        {0, nullptr},
    };
    PyType_Spec spec{classDefine->className.c_str(), sizeof(ScriptXHeapTypeObject), 0,
                     Py_TPFLAGS_HEAPTYPE, slots};
    PyObject* type = PyType_FromSpec(&spec);
    if (type == nullptr) {
      checkException();
      throw Exception("Failed to create type for class " + classDefine->className);
    }
    // Add static methods
    for (const auto& method : classDefine->staticDefine.functions) {
      PyObject_SetAttrString(
          type, method.name.c_str(),
          PyStaticMethod_New(warpFunction(method.name.c_str(), nullptr, METH_VARARGS,
                                          method.callback, PyImport_AddModule("__main__"),
                                          (PyTypeObject*)nullptr)));
          //py_backend::incRef(warpFunction(method.name.c_str(), nullptr, METH_VARARGS,
           //                               method.callback, PyImport_AddModule("__main__"),
           //                               (PyTypeObject*)nullptr)));
    }
    // Add static properties
    // for (const auto& property : classDefine->staticDefine.properties) {
    //   PyObject_SetAttrString(type, property.name.c_str(),
    //                          warpProperty(property.name.c_str(), nullptr, property.callback));
    // }
    nativeDefineRegistry_.emplace(classDefine, Global<Value>(Local<Value>(type)));
    set(String::newString(classDefine->className.c_str()), Local<Value>(type));
  }

  Local<Object> getNamespaceForRegister(const std::string_view& nameSpace) {
    TEMPLATE_NOT_IMPLEMENTED();
  }

  template <typename T>
  Local<Object> newNativeClassImpl(const ClassDefine<T>* classDefine, size_t size,
                                   const Local<Value>* args) {
    PyObject* tuple = PyTuple_New(size);
    for (size_t i = 0; i < size; ++i) {
      PyTuple_SetItem(tuple, i, py_interop::getLocal(args[i]));
    }

    PyTypeObject* type = reinterpret_cast<PyTypeObject*>(nativeDefineRegistry_[classDefine].val_);
    PyObject* obj = type->tp_new(type, tuple, nullptr);
    if (obj == nullptr) {
      checkException();
    }

    Py_DECREF(tuple);
    return Local<Object>(obj);
  }

  template <typename T>
  bool isInstanceOfImpl(const Local<Value>& value, const ClassDefine<T>* classDefine) {
    // TODO: 实现
    TEMPLATE_NOT_IMPLEMENTED();
  }

  template <typename T>
  T* getNativeInstanceImpl(const Local<Value>& value, const ClassDefine<T>* classDefine) {
    // TODO: 实现
    TEMPLATE_NOT_IMPLEMENTED();
  }

 private:
  template <typename T>
  friend class ::script::Local;

  template <typename T>
  friend class ::script::Global;

  template <typename T>
  friend class ::script::Weak;

  friend class ::script::Object;

  friend class ::script::Array;

  friend class ::script::Function;

  friend class ::script::ByteBuffer;

  friend class ::script::ScriptEngine;

  friend class ::script::Exception;

  friend class ::script::Arguments;

  friend class ::script::ScriptClass;

  friend class EngineScopeImpl;
};

}  // namespace script::py_backend