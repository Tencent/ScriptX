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
SCRIPTX_END_INCLUDE_LIBRARY

namespace script::py_backend {

inline PyObject* incRef(PyObject* ref) { return Py_XNewRef(ref); }

inline void decRef(PyObject* ref) { Py_XDECREF(ref); }

class PyEngine;

PyObject* checkException(PyObject* obj);
void checkException();
void rethrowException(const Exception& exception);
PyEngine* currentEngine();
PyEngine& currentEngineChecked();

PyObject* getGlobalDict();
PyObject* warpFunction(const char* name, const char* doc, int flags, FunctionCallback callback);

}  // namespace script::py_backend
