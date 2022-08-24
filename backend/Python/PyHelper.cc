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

PyObject* checkException(PyObject* obj) {
  if (!obj) {
    checkException();
  }
  return obj;
}

void checkException() {
  if (PyErr_Occurred()) {
    PyObject *pType, *pValue, *pTraceback;
    PyErr_Fetch(&pType, &pValue, &pTraceback);
    PyErr_NormalizeException(&pType, &pValue, &pTraceback);

    PyExceptionInfoStruct* errStruct = new PyExceptionInfoStruct;
    errStruct->pType = pType;
    errStruct->pValue = pValue;
    errStruct->pTraceback = pTraceback;

    PyObject* capsule = PyCapsule_New(errStruct, nullptr, [](PyObject* cap) {
      void* ptr = PyCapsule_GetPointer(cap, nullptr);
      delete static_cast<PyExceptionInfoStruct*>(ptr);
    });

    if (!capsule) return;
    throw Exception(py_interop::asLocal<Value>(capsule));
  }
}

void rethrowException(const Exception& exception) { throw exception; }

PyEngine* currentEngine() { return EngineScope::currentEngineAs<PyEngine>(); }
PyEngine& currentEngineChecked() { return EngineScope::currentEngineCheckedAs<PyEngine>(); }

PyObject* getGlobalDict() {
  PyObject* globals = PyEval_GetGlobals();
  if (globals == nullptr) {
    PyObject* mainName = PyUnicode_FromString("__main__");
    PyObject* __main__ = PyImport_GetModule(mainName);
    decRef(mainName);
    if (__main__ == nullptr) {
      __main__ = PyImport_AddModule("__main__");
    }
    if (__main__ == nullptr) {
      throw Exception("Empty __main__ in getGlobalDict!");
    }
    globals = PyModule_GetDict(__main__);
  }
  return globals;
}
}  // namespace script::py_backend
