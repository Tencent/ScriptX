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

#include "../../src/foundation.h"

// docs:
// https://docs.python.org/3/c-api/index.html
// https://docs.python.org/3/extending/embedding.html
// https://docs.python.org/3.8/c-api/init.html#thread-state-and-the-global-interpreter-lock

SCRIPTX_BEGIN_INCLUDE_LIBRARY
#include <Python.h>
#include <frameobject.h>
#include <structmember.h>
SCRIPTX_END_INCLUDE_LIBRARY

#if PY_VERSION_HEX < 0x030a00f0
#error "python version must be greater than 3.10.0"
#endif

namespace script::py_backend {

struct ExceptionInfo {
  PyObject* pType;
  PyObject* pValue;
  PyObject* pTraceback;
};

struct GeneralObject : PyObject {
  void* instance;
  PyObject* weakrefs;

  template <typename T>
  static T* getInstance(PyObject* self) {
    return reinterpret_cast<T*>(reinterpret_cast<GeneralObject*>(self)->instance);
  }

};

void setAttr(PyObject* obj, PyObject* key, PyObject* value);
void setAttr(PyObject* obj, const char* key, PyObject* value);
PyObject* getAttr(PyObject* obj, PyObject* key);
PyObject* getAttr(PyObject* obj, const char* key);
bool hasAttr(PyObject* obj, PyObject* key);
bool hasAttr(PyObject* obj, const char* key);
void delAttr(PyObject* obj, PyObject* key);
void delAttr(PyObject* obj, const char* key);

PyObject* toStr(const char* s);
PyObject* toStr(const std::string& s);

class PyEngine;

void checkPyErr();
void rethrowException(const Exception& exception);
PyEngine* currentEngine();
PyEngine* currentEngineChecked();

// @return borrowed ref
PyObject* getGlobalDict();
}  // namespace script::py_backend
