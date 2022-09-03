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

#include "PyHelper.hpp"
#include "PyEngine.h"

namespace script::py_backend {

void setAttr(PyObject* obj, PyObject* key, PyObject* value) {
  if (PyObject_SetAttr(obj, key, value) != 0) {
    throw Exception();
  }
}
void setAttr(PyObject* obj, const char* key, PyObject* value) {
  if (PyObject_SetAttrString(obj, key, value) != 0) {
    throw Exception();
  }
}

PyObject* getAttr(PyObject* obj, PyObject* key) {
  PyObject* result = PyObject_GetAttr(obj, key);
  if (!result) {
    throw Exception();
  }
  return result;
}

PyObject* getAttr(PyObject* obj, const char* key) {
  PyObject* result = PyObject_GetAttrString(obj, key);
  if (!result) {
    throw Exception();
  }
  return result;
}

bool hasAttr(PyObject* obj, PyObject* key) { return PyObject_HasAttr(obj, key) == 1; }

bool hasAttr(PyObject* obj, const char* key) { return PyObject_HasAttrString(obj, key) == 1; }

void delAttr(PyObject* obj, PyObject* key) {
  if (PyObject_DelAttr(obj, key) != 0) {
    throw Exception();
  }
}

void delAttr(PyObject* obj, const char* key) {
  if (PyObject_DelAttrString(obj, key) != 0) {
    throw Exception();
  }
}

PyObject* toStr(const char* s) { return PyUnicode_FromString(s); }

PyObject* toStr(const std::string& s) { return PyUnicode_FromStringAndSize(s.c_str(), s.size()); }

PyObject* checkException(PyObject* obj) {
  if (obj == nullptr) checkException();
  return obj;
}

void checkException() {
  if (PyErr_Occurred()) {
    PyObject *pType, *pValue, *pTraceback;
    PyErr_Fetch(&pType, &pValue, &pTraceback);
    PyErr_NormalizeException(&pType, &pValue, &pTraceback);

    ExceptionInfo* errStruct = new ExceptionInfo;
    errStruct->pType = pType;
    errStruct->pValue = pValue;
    errStruct->pTraceback = pTraceback;

    PyObject* capsule = PyCapsule_New(errStruct, nullptr, [](PyObject* cap) {
      void* ptr = PyCapsule_GetPointer(cap, nullptr);
      delete static_cast<ExceptionInfo*>(ptr);
    });

    if (!capsule) return;
    throw Exception(py_interop::asLocal<Value>(capsule));
  }
}

void rethrowException(const Exception& exception) { throw exception; }

PyEngine* currentEngine() { return EngineScope::currentEngineAs<PyEngine>(); }
PyEngine& currentEngineChecked() { return EngineScope::currentEngineCheckedAs<PyEngine>(); }

PyObject* getGlobalDict() {
  PyObject* m = PyImport_AddModule("__main__");
  if (m == nullptr) {
    throw Exception("can't find __main__ module");
  }
  return PyModule_GetDict(m);
}

inline PyObject* scriptx_static_get(PyObject* self, PyObject* /*ob*/, PyObject* cls) {
  return PyProperty_Type.tp_descr_get(self, cls, cls);
}

inline int scriptx_static_set(PyObject* self, PyObject* obj, PyObject* value) {
  PyObject* cls = PyType_Check(obj) ? obj : (PyObject*)Py_TYPE(obj);
  return PyProperty_Type.tp_descr_set(self, cls, value);
}

inline PyObject* scriptx_get_dict(PyObject* self, void*) {
  PyObject*& dict = *_PyObject_GetDictPtr(self);
  if (!dict) {
    dict = PyDict_New();
  }
  Py_XINCREF(dict);
  return dict;
}

inline int scriptx_set_dict(PyObject* self, PyObject* new_dict, void*) {
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

inline int scriptx_traverse(PyObject* self, visitproc visit, void* arg) {
  PyObject*& dict = *_PyObject_GetDictPtr(self);
  Py_VISIT(dict);
// https://docs.python.org/3/c-api/typeobj.html#c.PyTypeObject.tp_traverse
#if PY_VERSION_HEX >= 0x03090000
  Py_VISIT(Py_TYPE(self));
#endif
  return 0;
}

inline int scriptx_clear(PyObject* self) {
  PyObject*& dict = *_PyObject_GetDictPtr(self);
  Py_CLEAR(dict);
  return 0;
}

PyTypeObject* makeStaticPropertyType() {
  constexpr auto* name = "static_property";

  auto* heap_type = (PyHeapTypeObject*)PyType_Type.tp_alloc(&PyType_Type, 0);
  if (!heap_type) {
    Py_FatalError("error allocating type!");
  }

  heap_type->ht_name = PyUnicode_InternFromString(name);
  heap_type->ht_qualname = PyUnicode_InternFromString(name);

  auto* type = &heap_type->ht_type;
  type->tp_name = name;
  type->tp_base = &PyProperty_Type;
  type->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE;
  type->tp_descr_get = &scriptx_static_get;
  type->tp_descr_set = &scriptx_static_set;

  if (PyType_Ready(type) < 0) {
    Py_FatalError("failure in PyType_Ready()!");
  }

  setAttr((PyObject*)type, "__module__", PyUnicode_InternFromString("scriptx_builtins"));

  return type;
}

PyTypeObject* makeNamespaceType() {
  constexpr auto* name = "namespace";

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

  static PyGetSetDef getset[] = {{"__dict__", scriptx_get_dict, scriptx_set_dict, nullptr, nullptr},
                                 {nullptr, nullptr, nullptr, nullptr, nullptr}};
  type->tp_getset = getset;

  if (PyType_Ready(type) < 0) {
    Py_FatalError("failure in PyType_Ready()!");
  }
  setAttr((PyObject*)type, "__module__", PyUnicode_InternFromString("scriptx_builtins"));

  return type;
}

PyTypeObject* makeGenericType(const char* name) {
  auto heap_type = (PyHeapTypeObject*)PyType_GenericAlloc(&PyType_Type, 0);
  if (!heap_type) {
    Py_FatalError("error allocating type!");
  }

  heap_type->ht_name = PyUnicode_InternFromString(name);
  heap_type->ht_qualname = PyUnicode_InternFromString(name);

  auto* type = &heap_type->ht_type;
  type->tp_name = name;
  Py_INCREF(&PyProperty_Type);
  type->tp_base = &PyProperty_Type;
  type->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE;

  type->tp_descr_get = &scriptx_static_get;
  type->tp_descr_set = &scriptx_static_set;

  if (PyType_Ready(type) < 0) {
    Py_FatalError("failure in PyType_Ready()!");
  }

  setAttr((PyObject*)type, "__module__", PyUnicode_InternFromString("scriptx_builtins"));

  return type;
}

inline int scriptx_meta_setattro(PyObject* obj, PyObject* name, PyObject* value) {
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

inline PyObject* scriptx_meta_getattro(PyObject* obj, PyObject* name) {
  PyObject* descr = _PyType_Lookup((PyTypeObject*)obj, name);
  if (descr && PyInstanceMethod_Check(descr)) {
    Py_INCREF(descr);
    return descr;
  }
  return PyType_Type.tp_getattro(obj, name);
}

inline PyObject* scriptx_meta_call(PyObject* type, PyObject* args, PyObject* kwargs) {
  // use the default metaclass call to create/initialize the object
  PyObject* self = PyType_Type.tp_call(type, args, kwargs);
  if (self == nullptr) {
    return nullptr;
  }
#if 0
  // This must be a scriptx instance
  auto* instance = reinterpret_cast<detail::instance*>(self);

  // Ensure that the base __init__ function(s) were called
  for (const auto& vh : values_and_holders(instance)) {
    if (!vh.holder_constructed()) {
      PyErr_Format(PyExc_TypeError, "%.200s.__init__() must be called when overriding __init__",
                   get_fully_qualified_tp_name(vh.type->type).c_str());
      Py_DECREF(self);
      return nullptr;
    }
  }
#endif
  return self;
}

inline void scriptx_meta_dealloc(PyObject* obj) {
  auto* type = (PyTypeObject*)obj;
  auto engine = currentEngine();

  engine->registeredTypes_.erase(type);
  engine->registeredTypesReverse_.erase(type);
  PyType_Type.tp_dealloc(obj);
}

PyTypeObject* make_default_metaclass() {
  constexpr auto* name = "scriptx_type";
  auto name_obj = toStr(name);

  /* Danger zone: from now (and until PyType_Ready), make sure to
     issue no Python C API calls which could potentially invoke the
     garbage collector (the GC will call type_traverse(), which will in
     turn find the newly constructed type in an invalid state) */
  auto* heap_type = (PyHeapTypeObject*)PyType_Type.tp_alloc(&PyType_Type, 0);
  if (!heap_type) {
    Py_FatalError("make_default_metaclass(): error allocating metaclass!");
  }

  heap_type->ht_name = Py_NewRef(name_obj);
  heap_type->ht_qualname = Py_NewRef(name_obj);

  auto* type = &heap_type->ht_type;
  type->tp_name = name;
  Py_INCREF(&PyType_Type);
  type->tp_base = &PyType_Type;
  type->tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HEAPTYPE;

  type->tp_call = scriptx_meta_call;

  type->tp_setattro = scriptx_meta_setattro;
  type->tp_getattro = scriptx_meta_getattro;

  type->tp_dealloc = scriptx_meta_dealloc;

  if (PyType_Ready(type) < 0) {
    Py_FatalError("make_default_metaclass(): failure in PyType_Ready()!");
  }

  setAttr((PyObject*)type, "__module__", toStr("scriptx_builtins"));

  return type;
}

}  // namespace script::py_backend
