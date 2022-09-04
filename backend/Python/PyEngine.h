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

  std::unordered_map<const void*, PyTypeObject*> registeredTypes_;
  std::unordered_map<PyTypeObject*, const void*> registeredTypesReverse_;

  // Global thread state of main interpreter
  inline static PyThreadState* mainThreadState_ = nullptr;
  // Sub interpreter storage
  PyInterpreterState* subInterpreterState_;
  // Sub thread state of this sub interpreter (in TLS)
  TssStorage<PyThreadState> subThreadState_;

  // When you use EngineScope to enter a new engine(subinterpreter)
  // and find that there is an existing thread state owned by another engine,
  // we need to push its thread state to stack and release GIL to avoid dead-lock
  // -- see more code in "PyScope.cc"
  std::stack<PyThreadState*> oldThreadStateStack_;

  // Record global EngineScope enter times to determine
  // whether it is needed to unlock GIL when exit EngineScope
  // -- see more comments in "PyScope.cc"
  inline static int engineEnterCount_ = 0;

 public:
  inline static PyTypeObject* staticPropertyType_ = nullptr;
  inline static PyTypeObject* namespaceType_ = nullptr;
  inline static PyTypeObject* defaultMetaType_ = nullptr;

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
      setDictItem(nameSpaceObj, name.c_str(), value);
      Py_DECREF(value);
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
          sub = getDictItem(nameSpaceObj, key.c_str());
          if (sub == nullptr) {
            PyObject* args = PyTuple_New(0);
            PyTypeObject* type = reinterpret_cast<PyTypeObject*>(namespaceType_);
            sub = type->tp_new(type, args, nullptr);
            Py_DECREF(args);
            setDictItem(nameSpaceObj, key.c_str(), sub);
            Py_DECREF(sub);
          }
          setAttr(sub, name.c_str(), value);
          Py_DECREF(value);
        } else /*namespace type*/ {
          if (hasAttr(nameSpaceObj, key.c_str())) {
            sub = getAttr(nameSpaceObj, key.c_str());
          } else {
            PyObject* args = PyTuple_New(0);
            PyTypeObject* type = reinterpret_cast<PyTypeObject*>(namespaceType_);
            sub = type->tp_new(type, args, nullptr);
            Py_DECREF(args);
            setAttr(nameSpaceObj, key.c_str(), sub);
            Py_DECREF(sub);
          }
          setAttr(sub, name.c_str(), value);
          Py_DECREF(value);
        }
        nameSpaceObj = sub;
        begin = index + 1;
      }
    }
  }

  PyObject* warpGetter(const char* name, GetterCallback callback) {
    struct FunctionData {
      GetterCallback function;
      PyEngine* engine;
    };

    PyMethodDef* method = new PyMethodDef;
    method->ml_name = name;
    method->ml_flags = METH_VARARGS;
    method->ml_doc = nullptr;
    method->ml_meth = [](PyObject* self, PyObject* args) -> PyObject* {
      auto data = static_cast<FunctionData*>(PyCapsule_GetPointer(self, nullptr));
      return py_interop::peekPy(data->function());
    };

    PyCapsule_Destructor destructor = [](PyObject* cap) {
      void* ptr = PyCapsule_GetPointer(cap, nullptr);
      delete static_cast<FunctionData*>(ptr);
    };
    PyObject* capsule =
        PyCapsule_New(new FunctionData{std::move(callback), this}, nullptr, destructor);
    checkPyErr();

    PyObject* function = PyCFunction_New(method, capsule);
    Py_DECREF(capsule);
    checkPyErr();
    return function;
  }

  template <typename T>
  PyObject* warpInstanceGetter(const char* name, InstanceGetterCallback<T> callback) {
    struct FunctionData {
      InstanceGetterCallback<T> function;
      PyEngine* engine;
    };

    PyMethodDef* method = new PyMethodDef;
    method->ml_name = name;
    method->ml_flags = METH_VARARGS;
    method->ml_doc = nullptr;
    method->ml_meth = [](PyObject* self, PyObject* args) -> PyObject* {
      auto data = static_cast<FunctionData*>(PyCapsule_GetPointer(self, nullptr));
      T* thiz = GeneralObject::getInstance<T>(PyTuple_GetItem(args, 0));
      return py_interop::peekPy(data->function(thiz));
    };

    PyCapsule_Destructor destructor = [](PyObject* cap) {
      void* ptr = PyCapsule_GetPointer(cap, nullptr);
      delete static_cast<FunctionData*>(ptr);
    };
    PyObject* capsule =
        PyCapsule_New(new FunctionData{std::move(callback), this}, nullptr, destructor);
    checkPyErr();

    PyObject* function = PyCFunction_New(method, capsule);
    Py_DECREF(capsule);
    checkPyErr();

    return function;
  }

  PyObject* warpSetter(const char* name, SetterCallback callback) {
    struct FunctionData {
      SetterCallback function;
      PyEngine* engine;
    };

    PyMethodDef* method = new PyMethodDef;
    method->ml_name = name;
    method->ml_flags = METH_VARARGS;
    method->ml_doc = nullptr;
    method->ml_meth = [](PyObject* self, PyObject* args) -> PyObject* {
      auto data = static_cast<FunctionData*>(PyCapsule_GetPointer(self, nullptr));
      data->function(py_interop::toLocal<Value>(PyTuple_GetItem(args, 1)));
      Py_RETURN_NONE;
    };

    PyCapsule_Destructor destructor = [](PyObject* cap) {
      void* ptr = PyCapsule_GetPointer(cap, nullptr);
      delete static_cast<FunctionData*>(ptr);
    };
    PyObject* capsule =
        PyCapsule_New(new FunctionData{std::move(callback), this}, nullptr, destructor);
    checkPyErr();

    PyObject* function = PyCFunction_New(method, capsule);
    Py_DECREF(capsule);
    checkPyErr();

    return function;
  }

  template <typename T>
  PyObject* warpInstanceSetter(const char* name, InstanceSetterCallback<T> callback) {
    struct FunctionData {
      InstanceSetterCallback<T> function;
      PyEngine* engine;
    };

    PyMethodDef* method = new PyMethodDef;
    method->ml_name = name;
    method->ml_flags = METH_VARARGS;
    method->ml_doc = nullptr;
    method->ml_meth = [](PyObject* self, PyObject* args) -> PyObject* {
      auto data = static_cast<FunctionData*>(PyCapsule_GetPointer(self, nullptr));
      T* thiz = GeneralObject::getInstance<T>(PyTuple_GetItem(args, 0));
      data->function(thiz, py_interop::toLocal<Value>(PyTuple_GetItem(args, 1)));
      Py_RETURN_NONE;
    };

    PyCapsule_Destructor destructor = [](PyObject* cap) {
      void* ptr = PyCapsule_GetPointer(cap, nullptr);
      delete static_cast<FunctionData*>(ptr);
    };
    PyObject* capsule =
        PyCapsule_New(new FunctionData{std::move(callback), this}, nullptr, destructor);
    checkPyErr();

    PyObject* function = PyCFunction_New(method, capsule);
    Py_DECREF(capsule);
    checkPyErr();

    return function;
  }

  template <typename T>
  void registerStaticProperty(const ClassDefine<T>* classDefine, PyObject* type) {
    for (const auto& property : classDefine->staticDefine.properties) {
      PyObject* g = warpGetter(property.name.c_str(), property.getter);
      PyObject* s = warpSetter(property.name.c_str(), property.setter);
      PyObject* doc = toStr("");
      PyObject* warpped_property =
          PyObject_CallFunctionObjArgs((PyObject*)staticPropertyType_, g, s, Py_None, doc, nullptr);
      Py_DECREF(g);
      Py_DECREF(s);
      Py_DECREF(doc);
      setAttr(type, property.name.c_str(), warpped_property);
      Py_DECREF(warpped_property);
    }
  }

  template <typename T>
  void registerInstanceProperty(const ClassDefine<T>* classDefine, PyObject* type) {
    for (const auto& property : classDefine->instanceDefine.properties) {
      PyObject* g = warpInstanceGetter(property.name.c_str(), property.getter);
      PyObject* s = warpInstanceSetter(property.name.c_str(), property.setter);
      PyObject* doc = toStr("");
      PyObject* warpped_property =
          PyObject_CallFunctionObjArgs((PyObject*)&PyProperty_Type, g, s, Py_None, doc, nullptr);
      Py_DECREF(g);
      Py_DECREF(s);
      Py_DECREF(doc);
      setAttr(type, property.name.c_str(), warpped_property);
      Py_DECREF(warpped_property);
    }
  }

  template <typename T>
  void registerStaticFunction(const ClassDefine<T>* classDefine, PyObject* type) {
    for (const auto& f : classDefine->staticDefine.functions) {
      struct FunctionData {
        FunctionCallback function;
        py_backend::PyEngine* engine;
      };

      PyMethodDef* method = new PyMethodDef;
      method->ml_name = f.name.c_str();
      method->ml_flags = METH_VARARGS;
      method->ml_doc = nullptr;
      method->ml_meth = [](PyObject* self, PyObject* args) -> PyObject* {
        auto data = static_cast<FunctionData*>(PyCapsule_GetPointer(self, nullptr));
        return py_interop::peekPy(
            data->function(py_interop::makeArguments(data->engine, self, args)));
      };

      PyCapsule_Destructor destructor = [](PyObject* cap) {
        void* ptr = PyCapsule_GetPointer(cap, nullptr);
        delete static_cast<FunctionData*>(ptr);
      };
      PyObject* capsule =
          PyCapsule_New(new FunctionData{std::move(f.callback), this}, nullptr, destructor);
      checkPyErr();

      PyObject* function = PyCFunction_New(method, capsule);
      Py_DECREF(capsule);
      checkPyErr();

      PyObject* staticMethod = PyStaticMethod_New(function);
      Py_DECREF(function);
      setAttr(type, f.name.c_str(), staticMethod);
      Py_DECREF(staticMethod);
    }
  }

  template <typename T>
  void registerInstanceFunction(const ClassDefine<T>* classDefine, PyObject* type) {
    for (const auto& f : classDefine->instanceDefine.functions) {
      struct FunctionData {
        InstanceFunctionCallback<T> function;
        py_backend::PyEngine* engine;
      };

      PyMethodDef* method = new PyMethodDef;
      method->ml_name = f.name.c_str();
      method->ml_flags = METH_VARARGS;
      method->ml_doc = nullptr;
      method->ml_meth = [](PyObject* self, PyObject* args) -> PyObject* {
        auto data = static_cast<FunctionData*>(PyCapsule_GetPointer(self, nullptr));
        T* thiz = GeneralObject::getInstance<T>(PyTuple_GetItem(args, 0));
        PyObject* real_args = PyTuple_GetSlice(args, 1, PyTuple_Size(args));
        auto ret = data->function(thiz, py_interop::makeArguments(data->engine, self, real_args));
        Py_DECREF(real_args);
        return py_interop::peekPy(ret);
      };

      PyCapsule_Destructor destructor = [](PyObject* cap) {
        void* ptr = PyCapsule_GetPointer(cap, nullptr);
        delete static_cast<FunctionData*>(ptr);
      };
      PyObject* capsule =
          PyCapsule_New(new FunctionData{std::move(f.callback), this}, nullptr, destructor);
      checkPyErr();

      PyObject* function = PyCFunction_New(method, capsule);
      Py_DECREF(capsule);
      checkPyErr();

      PyObject* instanceMethod = PyInstanceMethod_New(function);
      Py_DECREF(function);
      setAttr(type, f.name.c_str(), instanceMethod);
      Py_DECREF(instanceMethod);
    }
  }

  template <typename T>
  void registerNativeClassImpl(const ClassDefine<T>* classDefine) {
    auto name_obj = toStr(classDefine->className.c_str());

    auto* heap_type = (PyHeapTypeObject*)PyType_GenericAlloc(PyEngine::defaultMetaType_, 0);
    if (!heap_type) {
      Py_FatalError("error allocating type!");
    }

    heap_type->ht_name = Py_NewRef(name_obj);
    heap_type->ht_qualname = Py_NewRef(name_obj);

    auto* type = &heap_type->ht_type;
    type->tp_name = classDefine->className.c_str();
    Py_INCREF(&PyBaseObject_Type);
    type->tp_base = &PyBaseObject_Type;
    type->tp_basicsize = static_cast<Py_ssize_t>(sizeof(GeneralObject));
    type->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE;

    type->tp_new = [](PyTypeObject* type, PyObject* args, PyObject* kwds) -> PyObject* {
      PyObject* self = type->tp_alloc(type, 0);
      if (type->tp_init(self, args, kwds) < 0) {
        throw Exception();
      }
      return self;
    };
    type->tp_init = [](PyObject* self, PyObject* args, PyObject* kwds) -> int {
      auto engine = currentEngine();
      auto classDefine =
          reinterpret_cast<const ClassDefine<T>*>(engine->registeredTypesReverse_[self->ob_type]);
      if (classDefine->instanceDefine.constructor) {
        reinterpret_cast<GeneralObject*>(self)->instance =
            classDefine->instanceDefine.constructor(py_interop::makeArguments(engine, self, args));
      } else {
        PyErr_Format(PyExc_Exception, "%s: no constructor", Py_TYPE(self)->tp_name);
        return -1;
      }
      return 0;
    };
    type->tp_dealloc = [](PyObject* self) {
      auto type = Py_TYPE(self);
      delete reinterpret_cast<GeneralObject*>(self)->instance;
      type->tp_free(self);
      Py_DECREF(type);
    };

    /* Support weak references (needed for the keep_alive feature) */
    type->tp_weaklistoffset = offsetof(GeneralObject, weakrefs);

    if (PyType_Ready(type) < 0) {
      Py_FatalError("PyType_Ready failed in make_object_base_type()");
    }

    setAttr((PyObject*)type, "__module__", toStr("scriptx_builtins"));

    this->registerStaticProperty(classDefine, (PyObject*)type);
    this->registerStaticFunction(classDefine, (PyObject*)type);
    this->registerInstanceProperty(classDefine, (PyObject*)type);
    this->registerInstanceFunction(classDefine, (PyObject*)type);
    this->registeredTypes_.emplace(classDefine, type);
    this->registeredTypesReverse_.emplace(type, classDefine);
    this->nameSpaceSet(classDefine, classDefine->className.c_str(), (PyObject*)type);
  }

  template <typename T>
  Local<Object> newNativeClassImpl(const ClassDefine<T>* classDefine, size_t size,
                                   const Local<Value>* args) {
    PyObject* tuple = PyTuple_New(size);
    for (size_t i = 0; i < size; ++i) {
      PyTuple_SetItem(tuple, i, args[i].val_);
      Py_DECREF(args[i].val_);
    }

    PyTypeObject* type = registeredTypes_[classDefine];
    PyObject* obj = type->tp_new(type, tuple, nullptr);
    Py_DECREF(tuple);
    return Local<Object>(obj);
  }

  template <typename T>
  bool isInstanceOfImpl(const Local<Value>& value, const ClassDefine<T>* classDefine) {
    return registeredTypes_[classDefine] == py_interop::peekPy(value)->ob_type;
  }

  template <typename T>
  T* getNativeInstanceImpl(const Local<Value>& value, const ClassDefine<T>* classDefine) {
    if (!isInstanceOfImpl(value, classDefine)) {
      throw Exception("Unmatched type of the value!");
    }
    return GeneralObject::getInstance<T>(py_interop::peekPy(value));
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

  friend class ExitEngineScopeImpl;

  friend PyTypeObject* makeDefaultMetaclass();
};

}  // namespace script::py_backend