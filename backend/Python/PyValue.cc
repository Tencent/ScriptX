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

#include "../../src/Exception.h"
#include "../../src/Reference.h"
#include "../../src/Scope.h"
#include "../../src/Value.h"
#include "PyHelper.hpp"

using script::py_interop;

namespace script {
/**
 * @return new ref.
 */
template <typename T>
Local<T> asLocalAndCheck(PyObject* ref) {
  return py_interop::toLocal<T>(py_backend::checkException(ref));
}

// for python this creates an empty dict
Local<Object> Object::newObject() { return asLocalAndCheck<Object>(PyDict_New()); }

Local<Object> Object::newObjectImpl(const Local<Value>& type, size_t size,
                                    const Local<Value>* args) {
  PyObject* dict = PyDict_New();
  if (!dict) {
    throw Exception("PyDict_New failed");
  }
  // TODO
  return asLocalAndCheck<Object>(dict);
}

Local<String> String::newString(const char* utf8) {
  return asLocalAndCheck<String>(PyUnicode_FromString(utf8));
}

Local<String> String::newString(std::string_view utf8) {
  return asLocalAndCheck<String>(PyUnicode_FromStringAndSize(utf8.data(), utf8.size()));
}

Local<String> String::newString(const std::string& utf8) {
  return newString(std::string_view(utf8));
}

#if defined(__cpp_char8_t)

Local<String> String::newString(const char8_t* utf8) {
  return newString(reinterpret_cast<const char*>(utf8));
}

Local<String> String::newString(std::u8string_view utf8) {
  return newString(std::string_view(reinterpret_cast<const char*>(utf8.data()), utf8.length()));
}

Local<String> String::newString(const std::u8string& utf8) {
  return newString(std::u8string_view(utf8));
}

#endif

Local<Number> Number::newNumber(float value) { return newNumber(static_cast<double>(value)); }

Local<Number> Number::newNumber(double value) {
  return asLocalAndCheck<Number>(PyFloat_FromDouble(value));
}

Local<Number> Number::newNumber(int32_t value) {
  return asLocalAndCheck<Number>(PyLong_FromLong(value));
}

Local<Number> Number::newNumber(int64_t value) {
  return asLocalAndCheck<Number>(PyLong_FromLongLong(value));
}

Local<Boolean> Boolean::newBoolean(bool value) {
  return asLocalAndCheck<Boolean>(PyBool_FromLong(value));
}

Local<Function> Function::newFunction(FunctionCallback callback) {
  return asLocalAndCheck<Function>(
      py_backend::warpFunction("ScriptX_Function", nullptr, METH_VARARGS, std::move(callback)));
}

Local<Array> Array::newArray(size_t size) { return asLocalAndCheck<Array>(PyList_New(size)); }

Local<Array> Array::newArrayImpl(size_t size, const Local<Value>* args) {
  auto list = PyList_New(size);
  if (!list) {
    throw Exception("PyList_New failed");
  }
  for (size_t i = 0; i < size; ++i) {
    PyList_SetItem(list, i, py_interop::getPy(args[i]));
  }
  return asLocalAndCheck<Array>(list);
}

Local<ByteBuffer> ByteBuffer::newByteBuffer(size_t size) {
  return asLocalAndCheck<ByteBuffer>(PyBytes_FromStringAndSize(nullptr, size));
}

Local<script::ByteBuffer> ByteBuffer::newByteBuffer(void* nativeBuffer, size_t size) {
  return asLocalAndCheck<ByteBuffer>(
      PyBytes_FromStringAndSize(static_cast<char*>(nativeBuffer), size));
}

Local<ByteBuffer> ByteBuffer::newByteBuffer(std::shared_ptr<void> nativeBuffer, size_t size) {
  return newByteBuffer(nativeBuffer.get(), size);
}

}  // namespace script