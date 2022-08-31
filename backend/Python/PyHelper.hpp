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
                            data->function(py_interop::toLocal<Value>(PyTuple_GetItem(args, 0)));
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
inline PyTypeObject* makeStaticPropertyType() {
  constexpr auto* name = "scriptx_static_property";

  /* Danger zone: from now (and until PyType_Ready), make sure to
     issue no Python C API calls which could potentially invoke the
     garbage collector (the GC will call type_traverse(), which will in
     turn find the newly constructed type in an invalid state) */
  auto* heap_type = (PyHeapTypeObject*)PyType_Type.tp_alloc(&PyType_Type, 0);
  if (!heap_type) {
    Py_FatalError("error allocating type!");
  }

  heap_type->ht_name = PyUnicode_InternFromString(name);
  heap_type->ht_qualname = PyUnicode_InternFromString(name);

  auto* type = &heap_type->ht_type;
  type->tp_name = name;
  type->tp_base = (PyTypeObject*)incRef((PyObject*)&PyProperty_Type);
  type->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE;
  type->tp_descr_get = scriptx_static_get;
  type->tp_descr_set = scriptx_static_set;

  if (PyType_Ready(type) < 0) {
    Py_FatalError("failure in PyType_Ready()!");
  }

  PyObject_SetAttrString((PyObject*)type, "__module__",
                         PyUnicode_InternFromString("scriptx_builtins"));

  return type;
}
/// dynamic_attr: Support for `d = instance.__dict__`.
extern "C" inline PyObject* scriptx_get_dict(PyObject* self, void*) {
  PyObject*& dict = *_PyObject_GetDictPtr(self);
  if (!dict) {
    dict = PyDict_New();
  }
  Py_XINCREF(dict);
  return dict;
}

/// dynamic_attr: Support for `instance.__dict__ = dict()`.
extern "C" inline int scriptx_set_dict(PyObject* self, PyObject* new_dict, void*) {
  if (!PyDict_Check(new_dict)) {
    PyErr_SetString(PyExc_TypeError, "__dict__ must be set to a dictionary");
    return -1;
  }
  PyObject*& dict = *_PyObject_GetDictPtr(self);
  Py_INCREF(new_dict);
  Py_CLEAR(dict);
  dict = new_dict;
  return 0;
}

/// dynamic_attr: Allow the garbage collector to traverse the internal instance `__dict__`.
extern "C" inline int scriptx_traverse(PyObject* self, visitproc visit, void* arg) {
  PyObject*& dict = *_PyObject_GetDictPtr(self);
  Py_VISIT(dict);
// https://docs.python.org/3/c-api/typeobj.html#c.PyTypeObject.tp_traverse
#if PY_VERSION_HEX >= 0x03090000
  Py_VISIT(Py_TYPE(self));
#endif
  return 0;
}

/// dynamic_attr: Allow the GC to clear the dictionary.
extern "C" inline int scriptx_clear(PyObject* self) {
  PyObject*& dict = *_PyObject_GetDictPtr(self);
  Py_CLEAR(dict);
  return 0;
}

inline PyTypeObject* makeNamespaceType() {
  constexpr auto* name = "scriptx_namespace";

  /* Danger zone: from now (and until PyType_Ready), make sure to
     issue no Python C API calls which could potentially invoke the
     garbage collector (the GC will call type_traverse(), which will in
     turn find the newly constructed type in an invalid state) */
  auto* heap_type = (PyHeapTypeObject*)PyType_Type.tp_alloc(&PyType_Type, 0);
  if (!heap_type) {
    Py_FatalError("error allocating type!");
  }

  heap_type->ht_name = PyUnicode_InternFromString(name);
  heap_type->ht_qualname = PyUnicode_InternFromString(name);

  auto* type = &heap_type->ht_type;
  type->tp_name = name;
  type->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_HEAPTYPE;

  type->tp_dictoffset = PyBaseObject_Type.tp_basicsize;  // place dict at the end
  type->tp_basicsize =
      PyBaseObject_Type.tp_basicsize + sizeof(PyObject*);  // and allocate enough space for it
  type->tp_traverse = scriptx_traverse;
  type->tp_clear = scriptx_clear;

  static PyGetSetDef getset[] = {
      {const_cast<char*>("__dict__"), scriptx_get_dict, scriptx_set_dict, nullptr, nullptr},
      {nullptr, nullptr, nullptr, nullptr, nullptr}};
  type->tp_getset = getset;

  if (PyType_Ready(type) < 0) {
    Py_FatalError("failure in PyType_Ready()!");
  }
  PyObject_SetAttrString((PyObject*)type, "__module__",
                         PyUnicode_InternFromString("scriptx_builtins"));

  return type;
}

inline PyTypeObject* makeGeneralType() {
  constexpr auto* name = "scriptx_namespace";

  /* Danger zone: from now (and until PyType_Ready), make sure to
     issue no Python C API calls which could potentially invoke the
     garbage collector (the GC will call type_traverse(), which will in
     turn find the newly constructed type in an invalid state) */
  auto* heap_type = (PyHeapTypeObject*)PyType_Type.tp_alloc(&PyType_Type, 0);
  if (!heap_type) {
    Py_FatalError("error allocating type!");
  }

  heap_type->ht_name = PyUnicode_InternFromString(name);
  heap_type->ht_qualname = PyUnicode_InternFromString(name);

  auto* type = &heap_type->ht_type;
  type->tp_name = name;
  type->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_HEAPTYPE;

  type->tp_dictoffset = PyBaseObject_Type.tp_basicsize;  // place dict at the end
  type->tp_basicsize =
      PyBaseObject_Type.tp_basicsize + sizeof(PyObject*);  // and allocate enough space for it
  type->tp_traverse = scriptx_traverse;
  type->tp_clear = scriptx_clear;

  static PyGetSetDef getset[] = {
      {const_cast<char*>("__dict__"), scriptx_get_dict, scriptx_set_dict, nullptr, nullptr},
      {nullptr, nullptr, nullptr, nullptr, nullptr}};
  type->tp_getset = getset;

  if (PyType_Ready(type) < 0) {
    Py_FatalError("failure in PyType_Ready()!");
  }
  PyObject_SetAttrString((PyObject*)type, "__module__",
                         PyUnicode_InternFromString("scriptx_builtins"));

  return type;
}

inline PyTypeObject* g_scriptx_property_type = nullptr;
inline PyTypeObject* g_scriptx_namespace_type = nullptr;
inline constexpr const char* g_class_define_string = "class_define";
}  // namespace py_backend
}  // namespace script
