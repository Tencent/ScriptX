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
  // @return new reference
  template <typename T>
  static Local<T> toLocal(PyObject* ref) {
    return Local<T>(Py_NewRef(ref));
  }

  // @return borrowed reference
  template <typename T>
  static Local<T> asLocal(PyObject* ref) {
    return Local<T>(ref);
  }

  // @return new reference
  template <typename T>
  static PyObject* getPy(const Local<T>& ref) {
    return Py_NewRef(ref.val_);
  }

  // @return borrowed reference
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
class TssStorage {
 private:
  Py_tss_t key = Py_tss_NEEDS_INIT;

 public:
  TssStorage() {
    int result = PyThread_tss_create(&key);  // TODO: Output or throw exception if failed
  }
  ~TssStorage() {
    if (isValid()) PyThread_tss_delete(&key);
  }
  int set(T* value) { return isValid() ? PyThread_tss_set(&key, (void*)value) : 1; }
  T* get() { return isValid() ? (T*)PyThread_tss_get(&key) : nullptr; }
  bool isValid() { return PyThread_tss_is_created(&key) != 0; }
};

// @return new reference
PyTypeObject* makeStaticPropertyType();
// @return new reference
PyTypeObject* makeNamespaceType();
// @return new reference
PyTypeObject* makeDefaultMetaclass();

}  // namespace py_backend
}  // namespace script
