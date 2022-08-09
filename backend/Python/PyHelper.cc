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
  auto err = PyErr_Occurred();
  if (err) {
    // TODO
    PyErr_Print();
  }
}

void rethrowException(const Exception& exception) { throw exception; }

PyEngine* currentEngine() { return EngineScope::currentEngineAs<PyEngine>(); }
PyEngine& currentEngineChecked() { return EngineScope::currentEngineCheckedAs<PyEngine>(); }

PyObject* getGlobalDict() {
  static PyObject* __main__ = nullptr;
  if (__main__ == nullptr) {
    __main__ = PyImport_ImportModule("__main__");
    //Py_DECREF(__main__);
  }
  PyObject* globals = PyModule_GetDict(__main__);
  return globals;
}

PyObject* warpFunction(const char* name, const char* doc, int flags, FunctionCallback callback) {
  // Function name can be nullptr
  // https://docs.python.org/zh-cn/3/c-api/capsule.html

  struct FunctionData {
    FunctionCallback function;
    py_backend::PyEngine* engine;
  };

  FunctionData* callbackIns = new FunctionData{std::move(callback), py_backend::currentEngine()};

  PyMethodDef* method = new PyMethodDef();
  method->ml_name = name;
  method->ml_doc = doc;
  method->ml_flags = flags;
  method->ml_meth = [](PyObject* self, PyObject* args) -> PyObject* {
    if (!PyCapsule_IsValid(self, nullptr)) throw Exception("Invalid function data");
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
  };

  PyObject* ctx = PyCapsule_New(callbackIns, nullptr, [](PyObject* cap) {
    void* ptr = PyCapsule_GetPointer(cap, nullptr);
    delete static_cast<FunctionData*>(ptr);
  });
  py_backend::checkException(ctx);
  callbackIns = nullptr;

  PyObject* closure = PyCFunction_NewEx(method, ctx, nullptr);
  Py_XDECREF(ctx);
  py_backend::checkException(closure);

  return closure;
}

}  // namespace script::py_backend
