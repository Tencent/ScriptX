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
#include "../../src/Reference.h"
#include "PyHelper.h"

namespace script {

class py_backend::PyEngine;

struct py_interop {
  /**
   * @return stolen ref(passing ownership).
   */
  template <typename T>
  static Local<T> makeLocal(PyObject* ref) {
    return Local<T>(ref);
  }

  /**
   * @return stolen ref.
   */
  template <typename T>
  static PyObject* getLocal(const Local<T>& ref) {
    return py_backend::incRef(ref.val_);
  }

  /**
   * @return borrowed ref.
   */
  template <typename T>
  static PyObject* peekLocal(const Local<T>& ref) {
    return ref.val_;
  }

  static Arguments makeArguments(py_backend::PyEngine* engine, PyObject* self, PyObject* args) {
    return Arguments(py_backend::ArgumentsData{engine, self, args});
  }
};

namespace py_backend {

class PyTssStorage {
 private:
  Py_tss_t key = Py_tss_NEEDS_INIT;

 public:
  PyTssStorage() {
    int result = PyThread_tss_create(&key);  // TODO: Output or throw exception if failed
  }
  ~PyTssStorage() {
    if (isValid()) PyThread_tss_delete(&key);
  }
  int set(void* value) { return isValid() ? PyThread_tss_set(&key, value) : 1; }
  void* get() { return isValid() ? PyThread_tss_get(&key) : NULL; }
  bool isValid() { return PyThread_tss_is_created(&key) > 0; }
};

template <typename T>
struct ScriptXHeapTypeObject {
  PyObject_HEAD;
  const ClassDefine<T>* classDefine;
  T* instance;
};

inline PyObject* warpFunction(const char* name, const char* doc, int flags,
                              FunctionCallback callback) {
  // Function name can be nullptr
  // https://docs.python.org/zh-cn/3/c-api/capsule.html

  struct FunctionData {
    FunctionCallback function;
    py_backend::PyEngine* engine;
  };

  FunctionData* callbackIns = new FunctionData{std::move(callback), py_backend::currentEngine()};

  PyMethodDef* method = new PyMethodDef{
      name,
      [](PyObject* self, PyObject* args) -> PyObject* {
        if (!PyCapsule_IsValid(self, nullptr)) {
          throw Exception("Invalid function data");
        }
        void* ptr = PyCapsule_GetPointer(self, nullptr);
        if (ptr == nullptr) {
          PyErr_SetString(PyExc_TypeError, "invalid 'self' for native method");
        } else {
          auto data = static_cast<FunctionData*>(ptr);
          try {
            auto ret = data->function(py_interop::makeArguments(data->engine, self, args));
            return py_interop::getLocal(ret);
          } catch (const Exception& e) {
            py_backend::rethrowException(e);
          }
        }
        return nullptr;
      },
      flags, doc};

  PyObject* capsule = PyCapsule_New(callbackIns, nullptr, [](PyObject* cap) {
    void* ptr = PyCapsule_GetPointer(cap, nullptr);
    delete static_cast<FunctionData*>(ptr);
  });
  py_backend::checkException(capsule);
  callbackIns = nullptr;

  PyObject* closure = PyCFunction_New(method, capsule);
  Py_XDECREF(capsule);
  py_backend::checkException(closure);

  return closure;
}

template <typename T>
PyObject* warpInstanceFunction(const char* name, const char* doc, int flags,
                               InstanceFunctionCallback<T> callback) {
  // Function name can be nullptr
  // https://docs.python.org/zh-cn/3/c-api/capsule.html

  struct FunctionData {
    InstanceFunctionCallback<T> function;
    py_backend::PyEngine* engine;
  };

  FunctionData* callbackIns = new FunctionData{std::move(callback), py_backend::currentEngine()};

  PyMethodDef* method = new PyMethodDef{
      name,
      [](PyObject* self, PyObject* args) -> PyObject* {
        if (!PyCapsule_IsValid(self, nullptr)) {
          throw Exception("Invalid function data");
        }
        void* ptr = PyCapsule_GetPointer(self, nullptr);
        if (ptr == nullptr) {
          PyErr_SetString(PyExc_TypeError, "invalid 'self' for native method");
        } else {
          auto data = static_cast<FunctionData*>(ptr);
          try {
            T* thiz =
                reinterpret_cast<ScriptXHeapTypeObject<T>*>(PyTuple_GetItem(args, 0))->instance;
            auto ret = data->function(thiz, py_interop::makeArguments(data->engine, self, args));
            return py_interop::getLocal(ret);
          } catch (const Exception& e) {
            py_backend::rethrowException(e);
          }
        }
        return nullptr;
      },
      flags, doc};

  PyObject* capsule = PyCapsule_New(callbackIns, nullptr, [](PyObject* cap) {
    void* ptr = PyCapsule_GetPointer(cap, nullptr);
    delete static_cast<FunctionData*>(ptr);
  });
  py_backend::checkException(capsule);
  callbackIns = nullptr;

  PyObject* closure = PyCFunction_New(method, capsule);
  Py_XDECREF(capsule);
  py_backend::checkException(closure);

  return closure;
}

/// `scriptx_static_property.__get__()`: Always pass the class instead of the instance.
extern "C" inline PyObject* scriptx_static_get(PyObject* self, PyObject* /*ob*/, PyObject* cls) {
  return PyProperty_Type.tp_descr_get(self, cls, cls);
}

/// `scriptx_static_property.__set__()`: Just like the above `__get__()`.
extern "C" inline int scriptx_static_set(PyObject* self, PyObject* obj, PyObject* value) {
  PyObject* cls = PyType_Check(obj) ? obj : (PyObject*)Py_TYPE(obj);
  return PyProperty_Type.tp_descr_set(self, cls, value);
}
/** A `static_property` is the same as a `property` but the `__get__()` and `__set__()`
      methods are modified to always use the object type instead of a concrete instance.
      Return value: New reference. */
inline PyObject* makeStaticPropertyType() {
  PyType_Slot slots[] = {
      {Py_tp_base, py_backend::incRef((PyObject*)&PyProperty_Type)},
      {Py_tp_descr_get, scriptx_static_get},
      {Py_tp_descr_set, scriptx_static_set},
      {0, nullptr},
  };
  PyType_Spec spec{"scriptx_static_property", PyProperty_Type.tp_basicsize,
                   PyProperty_Type.tp_itemsize,
                   Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE, slots};
  PyObject* type = PyType_FromSpec(&spec);
  return type;
}
inline PyObject* g_scriptx_property_type = nullptr;

}  // namespace py_backend
}  // namespace script
