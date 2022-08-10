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

}  // namespace script

namespace script::py_backend {

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

}  // namespace script::py_backend