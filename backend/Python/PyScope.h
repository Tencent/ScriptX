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
#include "../../src/Reference.h"
#include "PyHelper.h"

namespace script::py_backend {

class PyEngine;

class PyEngineScopeImpl {

 public:
  explicit PyEngineScopeImpl(PyEngine &, PyEngine *);

  ~PyEngineScopeImpl();
};

class ExitPyEngineScopeImpl {

 public:
  explicit PyExitEngineScopeImpl(PyEngine &);

  ~PyExitEngineScopeImpl() = default;
};

class PyStackFrameScopeImpl {
 public:
  explicit PyStackFrameScopeImpl(PyEngine &) {}

  template <typename T>
  Local<T> returnValue(const Local<T> &localRef) {
    return localRef;
  }
};
}  // namespace script::py_backend
