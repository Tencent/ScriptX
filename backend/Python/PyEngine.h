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

#include <stack>
#include "../../src/Engine.h"
#include "../../src/Exception.h"
#include "../../src/utils/Helper.hpp"
#include "../../src/utils/MessageQueue.h"
#include "PyHelper.hpp"

namespace script::py_backend {

// an PyEngine = a subinterpreter
class PyEngine : public ScriptEngine {
 private:
  std::shared_ptr<::script::utils::MessageQueue> queue_;

  std::unordered_map<const void*, Global<Value>> nativeDefineRegistry_;

  // Global thread state of main interpreter
  inline static PyThreadState* mainThreadState_ = nullptr;
  // Sub interpreter storage
  PyInterpreterState* subInterpreterState_;
  // Sub thread state of this sub interpreter (in TLS)
  PyTssStorage<PyThreadState> subThreadState_;

  // When you use EngineScope to enter a new engine(subinterpreter)
  // and find that there is an existing thread state owned by another engine,
  // we need to push its thread state to stack and release GIL to avoid dead-lock
  // -- see more code in "PyScope.cc"
  std::stack<PyThreadState*> oldThreadStateStack_;

  // Record global EngineScope enter times to determine
  // whether it is needed to unlock GIL when exit EngineScope
  // -- see more comments in "PyScope.cc"
  static inline int engineEnterCount;

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
  void nameSpaceSet(const ClassDefine<T>* classDefine, const std::string& name, PyObject* value) {
    std::string nameSpace = classDefine->getNameSpace();
    PyObject* nameSpaceObj = getGlobalDict();

    if (nameSpace.empty()) {
      PyDict_SetItemString(nameSpaceObj, name.c_str(), value);
    } else {  // namespace can be aaa.bbb.ccc
      std::size_t begin = 0;
      while (begin < nameSpace.size()) {
        auto index = nameSpace.find('.', begin);
        if (index == std::string::npos) {
          index = nameSpace.size();
        }

        PyObject* sub = nullptr;
        auto key = nameSpace.substr(begin, index - begin);
        if (PyDict_CheckExact(nameSpaceObj)) {
          sub = PyDict_GetItemString(nameSpaceObj, key.c_str());
          if (sub == nullptr) {
            PyObject* args = PyTuple_New(0);
            PyTypeObject* type = reinterpret_cast<PyTypeObject*>(g_namespace_type);
            sub = type->tp_new(type, args, nullptr);
            decRef(args);
            PyDict_SetItemString(nameSpaceObj, key.c_str(), sub);
          }
          setAttr(sub, name.c_str(), value);
        } else /*namespace type*/ {
          if (hasAttr(nameSpaceObj, key.c_str())) {
            sub = getAttr(nameSpaceObj, key.c_str());
          } else {
            PyObject* args = PyTuple_New(0);
            PyTypeObject* type = reinterpret_cast<PyTypeObject*>(g_namespace_type);
            sub = type->tp_new(type, args, nullptr);
            decRef(args);
            setAttr(nameSpaceObj, key.c_str(),sub);
          }
          setAttr(sub, name.c_str(), value);
        }
        nameSpaceObj = sub;
        begin = index + 1;
      }
    }
  }

  template <typename T>
  void registerStaticProperty(const ClassDefine<T>* classDefine, PyObject* type) {
    for (const auto& property : classDefine->staticDefine.properties) {
      PyObject* doc = toStr("");
      PyObject* args = PyTuple_Pack(
          4, warpGetter(property.name.c_str(), nullptr, METH_VARARGS, property.getter),
          warpSetter(property.name.c_str(), nullptr, METH_VARARGS, property.setter), Py_None, doc);
      decRef(doc);
      PyObject* warpped_property = PyObject_Call((PyObject*)g_static_property_type, args, nullptr);
      setAttr(type, property.name.c_str(), warpped_property);
    }
  }

  template <typename T>
  void registerInstanceProperty(const ClassDefine<T>* classDefine, PyObject* type) {
    for (const auto& property : classDefine->instanceDefine.properties) {
      PyObject* doc = toStr("");
      PyObject* args = PyTuple_Pack(
          4, warpInstanceGetter(property.name.c_str(), nullptr, METH_VARARGS, property.getter),
          warpInstanceSetter(property.name.c_str(), nullptr, METH_VARARGS, property.setter),
          Py_None, doc);
      decRef(doc);
      PyObject* warpped_property = PyObject_Call((PyObject*)&PyProperty_Type, args, nullptr);
      setAttr(type, property.name.c_str(), warpped_property);
    }
  }

  template <typename T>
  void registerStaticFunction(const ClassDefine<T>* classDefine, PyObject* type) {
    for (const auto& method : classDefine->staticDefine.functions) {
      PyObject* function = PyStaticMethod_New(
          warpFunction(method.name.c_str(), nullptr, METH_VARARGS, method.callback));
      setAttr(type, method.name.c_str(), function);
    }
  }

  template <typename T>
  void registerInstanceFunction(const ClassDefine<T>* classDefine, PyObject* type) {
    for (const auto& method : classDefine->instanceDefine.functions) {
      PyObject* function = PyInstanceMethod_New(
          warpInstanceFunction(method.name.c_str(), nullptr, METH_VARARGS, method.callback));
      setAttr(type, method.name.c_str(), function);
    }
  }

  template <typename T>
  void registerNativeClassImpl(const ClassDefine<T>* classDefine) {
    bool constructable = bool(classDefine->instanceDefine.constructor);

    auto* res = (PyHeapTypeObject*)PyType_GenericAlloc(&PyType_Type, 0);
    if (!res) {
      Py_FatalError("error allocating type!");
    }

    res->ht_name = toStr(classDefine->className.c_str());
    res->ht_qualname = toStr(classDefine->className.c_str());

    auto* type = &res->ht_type;
    type->tp_name = classDefine->className.c_str();

    type->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HEAPTYPE;
    if (!constructable) type->tp_flags |= Py_TPFLAGS_DISALLOW_INSTANTIATION;

    type->tp_base = &PyBaseObject_Type;
    type->tp_basicsize = sizeof(ScriptXPyObject<T>);

    /* Initialize essential fields */
    type->tp_as_async = &res->as_async;
    type->tp_as_number = &res->as_number;
    type->tp_as_sequence = &res->as_sequence;
    type->tp_as_mapping = &res->as_mapping;
    type->tp_as_buffer = &res->as_buffer;

    type->tp_setattro = &scriptx_meta_setattro;
    type->tp_getattro = &scriptx_meta_getattro;

    if (constructable) {
      type->tp_new = static_cast<newfunc>(
          [](PyTypeObject* subtype, PyObject* args, PyObject* kwds) -> PyObject* {
            PyObject* thiz = subtype->tp_alloc(subtype, subtype->tp_basicsize);
            subtype->tp_init(thiz, args, kwds);
            return thiz;
          });
      type->tp_dealloc = static_cast<destructor>([](PyObject* self) {
        auto thiz = reinterpret_cast<ScriptXPyObject<T>*>(self);
        delete thiz->instance;
        Py_TYPE(self)->tp_free(self);
      });
      type->tp_init =
          static_cast<initproc>([](PyObject* self, PyObject* args, PyObject* kwds) -> int {
            auto classDefine = reinterpret_cast<const ClassDefine<T>*>(PyCapsule_GetPointer(
                getAttr((PyObject*)self->ob_type, g_class_define_string), nullptr));
            auto thiz = reinterpret_cast<ScriptXPyObject<T>*>(self);
            if (classDefine->instanceDefine.constructor) {
              thiz->instance = classDefine->instanceDefine.constructor(
                  py_interop::makeArguments(currentEngine(), self, args));
            }
            return 0;
          });
    }

    if (PyType_Ready(type) < 0) {
      Py_FatalError("failure in PyType_Ready()!");
    }
    setAttr((PyObject*)type, "__module__", toStr("scriptx_builtins"));

    setAttr((PyObject*)type, g_class_define_string,
            PyCapsule_New((void*)classDefine, nullptr, nullptr));
    this->registerStaticProperty(classDefine, (PyObject*)type);
    this->registerStaticFunction(classDefine, (PyObject*)type);
    if (constructable) {
      this->registerInstanceProperty(classDefine, (PyObject*)type);
      this->registerInstanceFunction(classDefine, (PyObject*)type);
    }
    this->nativeDefineRegistry_.emplace(classDefine,
                                        Global<Value>(py_interop::asLocal<Value>((PyObject*)type)));
    this->nameSpaceSet(classDefine, classDefine->className.c_str(), (PyObject*)type);
  }

  template <typename T>
  Local<Object> newNativeClassImpl(const ClassDefine<T>* classDefine, size_t size,
                                   const Local<Value>* args) {
    PyObject* tuple = PyTuple_New(size);
    for (size_t i = 0; i < size; ++i) {
      PyTuple_SetItem(tuple, i, py_interop::getPy(args[i]));
    }

    PyTypeObject* type = reinterpret_cast<PyTypeObject*>(nativeDefineRegistry_[classDefine].val_);
    PyObject* obj = type->tp_new(type, tuple, nullptr);
    Py_DECREF(tuple);
    return Local<Object>(obj);
  }

  template <typename T>
  bool isInstanceOfImpl(const Local<Value>& value, const ClassDefine<T>* classDefine) {
    PyObject* capsule = getAttr(getType(py_interop::peekPy(value)), g_class_define_string);
    if (capsule == nullptr) return false;
    return PyCapsule_GetPointer(capsule, nullptr) == classDefine;
  }

  template <typename T>
  T* getNativeInstanceImpl(const Local<Value>& value, const ClassDefine<T>* classDefine) {
    if (!isInstanceOfImpl(value, classDefine)) {
      throw Exception("Unmatched type of the value!");
    }
    return reinterpret_cast<ScriptXPyObject<T>*>(py_interop::peekPy(value))->instance;
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