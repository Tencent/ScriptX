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

class PyEngine;

struct py_interop {
  /**
   * @return new ref
   */
  template <typename T>
  static Local<T> toLocal(PyObject* ref) {
    return Local<T>(py_backend::incRef(ref));
  }

  /**
   * @return borrowed ref
   */
  template <typename T>
  static Local<T> asLocal(PyObject* ref) {
    return Local<T>(ref);
  }

  /**
   * @return new ref.
   */
  template <typename T>
  static PyObject* getPy(const Local<T>& ref) {
    return py_backend::incRef(ref.val_);
  }

  /**
   * @return borrowed ref.
   */
  template <typename T>
  static PyObject* peekPy(const Local<T>& ref) {
    return ref.val_;
  }

  static Arguments makeArguments(py_backend::PyEngine* engine, PyObject* self, PyObject* args) {
    return Arguments(py_backend::ArgumentsData{engine, self, args});
  }
};

namespace py_backend {

template <typename T>
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
  int set(T* value) { return isValid() ? PyThread_tss_set(&key, (void*)value) : 1; }
  T* get() { return isValid() ? (T*)PyThread_tss_get(&key) : NULL; }
  bool isValid() { return PyThread_tss_is_created(&key) > 0; }
};

template <typename T>
struct ScriptXPyObject {
  PyObject_HEAD;
  T* instance;
};

inline PyObject* warpFunction(const char* name, const char* doc, int flags,
                              FunctionCallback callback) {
  struct FunctionData {
    FunctionCallback function;
    PyEngine* engine;
  };

  FunctionData* callbackIns = new FunctionData{std::move(callback), currentEngine()};

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
            return py_interop::peekPy(
                data->function(py_interop::makeArguments(data->engine, self, args)));
          } catch (const Exception& e) {
            rethrowException(e);
          }
        }
        return nullptr;
      },
      flags, doc};

  PyObject* capsule = PyCapsule_New(callbackIns, nullptr, [](PyObject* cap) {
    void* ptr = PyCapsule_GetPointer(cap, nullptr);
    delete static_cast<FunctionData*>(ptr);
  });
  checkException(capsule);
  callbackIns = nullptr;

  PyObject* closure = PyCFunction_New(method, capsule);
  decRef(capsule);
  checkException(closure);

  return closure;
}

template <typename T>
inline PyObject* warpInstanceFunction(const char* name, const char* doc, int flags,
                                      InstanceFunctionCallback<T> callback) {
  struct FunctionData {
    InstanceFunctionCallback<T> function;
    PyEngine* engine;
  };

  FunctionData* callbackIns = new FunctionData{std::move(callback), currentEngine()};

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
            T* thiz = reinterpret_cast<ScriptXPyObject<T>*>(PyTuple_GetItem(args, 0))->instance;
            PyObject* real_args = PyTuple_GetSlice(args, 1, PyTuple_Size(args));
            auto ret =
                data->function(thiz, py_interop::makeArguments(data->engine, self, real_args));
            decRef(real_args);
            return py_interop::peekPy(ret);
          } catch (const Exception& e) {
            rethrowException(e);
          }
        }
        return nullptr;
      },
      flags, doc};

  PyObject* capsule = PyCapsule_New(callbackIns, nullptr, [](PyObject* cap) {
    void* ptr = PyCapsule_GetPointer(cap, nullptr);
    delete static_cast<FunctionData*>(ptr);
  });
  checkException(capsule);
  callbackIns = nullptr;

  PyObject* closure = PyCFunction_New(method, capsule);
  decRef(capsule);
  checkException(closure);

  return closure;
}

inline PyObject* warpGetter(const char* name, const char* doc, int flags, GetterCallback callback) {
  struct FunctionData {
    GetterCallback function;
    PyEngine* engine;
  };

  FunctionData* callbackIns = new FunctionData{std::move(callback), currentEngine()};

  PyMethodDef* method =
      new PyMethodDef{name,
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
                            return py_interop::peekPy(data->function());
                          } catch (const Exception& e) {
                            rethrowException(e);
                          }
                        }
                        return nullptr;
                      },
                      flags, doc};

  PyObject* capsule = PyCapsule_New(callbackIns, nullptr, [](PyObject* cap) {
    void* ptr = PyCapsule_GetPointer(cap, nullptr);
    delete static_cast<FunctionData*>(ptr);
  });
  checkException(capsule);
  callbackIns = nullptr;

  PyObject* closure = PyCFunction_New(method, capsule);
  decRef(capsule);
  checkException(closure);

  return closure;
}

template <typename T>
inline PyObject* warpInstanceGetter(const char* name, const char* doc, int flags,
                                    InstanceGetterCallback<T> callback) {
  struct FunctionData {
    InstanceGetterCallback<T> function;
    PyEngine* engine;
  };

  FunctionData* callbackIns = new FunctionData{std::move(callback), currentEngine()};

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
            T* thiz = reinterpret_cast<ScriptXPyObject<T>*>(PyTuple_GetItem(args, 0))->instance;
            return py_interop::peekPy(data->function(thiz));
          } catch (const Exception& e) {
            rethrowException(e);
          }
        }
        return nullptr;
      },
      flags, doc};

  PyObject* capsule = PyCapsule_New(callbackIns, nullptr, [](PyObject* cap) {
    void* ptr = PyCapsule_GetPointer(cap, nullptr);
    delete static_cast<FunctionData*>(ptr);
  });
  checkException(capsule);
  callbackIns = nullptr;

  PyObject* closure = PyCFunction_New(method, capsule);
  decRef(capsule);
  checkException(closure);

  return closure;
}

inline PyObject* warpSetter(const char* name, const char* doc, int flags, SetterCallback callback) {
  struct FunctionData {
    SetterCallback function;
    PyEngine* engine;
  };

  FunctionData* callbackIns = new FunctionData{std::move(callback), currentEngine()};

  PyMethodDef* method =
      new PyMethodDef{name,
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
                            data->function(py_interop::toLocal<Value>(PyTuple_GetItem(args, 1)));
                            Py_RETURN_NONE;
                          } catch (const Exception& e) {
                            rethrowException(e);
                          }
                        }
                        return nullptr;
                      },
                      flags, doc};

  PyObject* capsule = PyCapsule_New(callbackIns, nullptr, [](PyObject* cap) {
    void* ptr = PyCapsule_GetPointer(cap, nullptr);
    delete static_cast<FunctionData*>(ptr);
  });
  checkException(capsule);
  callbackIns = nullptr;

  PyObject* closure = PyCFunction_New(method, capsule);
  decRef(capsule);
  checkException(closure);

  return closure;
}

template <typename T>
PyObject* warpInstanceSetter(const char* name, const char* doc, int flags,
                             InstanceSetterCallback<T> callback) {
  struct FunctionData {
    InstanceSetterCallback<T> function;
    PyEngine* engine;
  };

  FunctionData* callbackIns = new FunctionData{std::move(callback), currentEngine()};

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
            T* thiz = reinterpret_cast<ScriptXPyObject<T>*>(PyTuple_GetItem(args, 0))->instance;
            data->function(thiz, py_interop::toLocal<Value>(PyTuple_GetItem(args, 1)));
            Py_RETURN_NONE;
          } catch (const Exception& e) {
            rethrowException(e);
          }
        }
        return nullptr;
      },
      flags, doc};

  PyObject* capsule = PyCapsule_New(callbackIns, nullptr, [](PyObject* cap) {
    void* ptr = PyCapsule_GetPointer(cap, nullptr);
    delete static_cast<FunctionData*>(ptr);
  });
  checkException(capsule);
  callbackIns = nullptr;

  PyObject* closure = PyCFunction_New(method, capsule);
  decRef(capsule);
  checkException(closure);

  return closure;
}
/** A `static_property` is the same as a `property` but the `__get__()` and `__set__()`
      methods are modified to always use the object type instead of a concrete instance.
      Return value: New reference. */
PyTypeObject* makeStaticPropertyType();
PyTypeObject* makeNamespaceType();
PyTypeObject* makeGenericType(const char* name);

inline PyTypeObject* g_static_property_type = nullptr;
inline PyTypeObject* g_namespace_type = nullptr;
inline constexpr auto* g_class_define_string = "class_define";

/** Types with static properties need to handle `Type.static_prop = x` in a specific way.
    By default, Python replaces the `static_property` itself, but for wrapped C++ types
    we need to call `static_property.__set__()` in order to propagate the new value to
    the underlying C++ data structure. */
extern "C" inline int scriptx_meta_setattro(PyObject* obj, PyObject* name, PyObject* value) {
  // Use `_PyType_Lookup()` instead of `PyObject_GetAttr()` in order to get the raw
  // descriptor (`property`) instead of calling `tp_descr_get` (`property.__get__()`).
  PyObject* descr = _PyType_Lookup((PyTypeObject*)obj, name);

  // The following assignment combinations are possible:
  //   1. `Type.static_prop = value`             --> descr_set: `Type.static_prop.__set__(value)`
  //   2. `Type.static_prop = other_static_prop` --> setattro:  replace existing `static_prop`
  //   3. `Type.regular_attribute = value`       --> setattro:  regular attribute assignment
  auto* const static_prop = (PyObject*)g_static_property_type;
  const auto call_descr_set = (descr != nullptr) && (value != nullptr) &&
                              (PyObject_IsInstance(descr, static_prop) != 0) &&
                              (PyObject_IsInstance(value, static_prop) == 0);
  if (call_descr_set) {
    // Call `static_property.__set__()` instead of replacing the `static_property`.
    return Py_TYPE(descr)->tp_descr_set(descr, obj, value);
  } else {
    // Replace existing attribute.
    return PyType_Type.tp_setattro(obj, name, value);
  }
}

/**
 * Python 3's PyInstanceMethod_Type hides itself via its tp_descr_get, which prevents aliasing
 * methods via cls.attr("m2") = cls.attr("m1"): instead the tp_descr_get returns a plain function,
 * when called on a class, or a PyMethod, when called on an instance.  Override that behaviour here
 * to do a special case bypass for PyInstanceMethod_Types.
 */
extern "C" inline PyObject* scriptx_meta_getattro(PyObject* obj, PyObject* name) {
  PyObject* descr = _PyType_Lookup((PyTypeObject*)obj, name);
  if (descr && PyInstanceMethod_Check(descr)) {
    Py_INCREF(descr);
    return descr;
  }
  return PyType_Type.tp_getattro(obj, name);
}
}  // namespace py_backend
}  // namespace script
