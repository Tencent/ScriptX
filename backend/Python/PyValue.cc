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

template <typename T>
Local<T> checkAndMakeLocal(py::object ref) {
  return py_interop::makeLocal<T>(ref);
}

// for python this creates an empty dict
Local<Object> Object::newObject() { return Local<Object>(py::dict()); }

Local<Object> Object::newObjectImpl(const Local<Value>& type, size_t size,
                                    const Local<Value>* args) {
  py::dict dict;
  return Local<Object>(dict);
}

Local<String> String::newString(const char* utf8) { return Local<String>(py::str(utf8)); }

Local<String> String::newString(std::string_view utf8) { return Local<String>(py::str(utf8)); }

Local<String> String::newString(const std::string& utf8) { return Local<String>(py::str(utf8)); }

#if defined(__cpp_char8_t)

Local<String> String::newString(const char8_t* utf8) {
  return newString(reinterpret_cast<const char*>(utf8));
}

Local<String> String::newString(std::u8string_view utf8) {
  return newString(std::string_view(reinterpret_cast<const char*>(utf8.data()), utf8.length()));
}

Local<String> String::newString(const std::u8string& utf8) { return newString(utf8.c_str()); }

#endif

Local<Number> Number::newNumber(float value) { return newNumber(static_cast<double>(value)); }

Local<Number> Number::newNumber(double value) { return Local<Number>(py::float_(value)); }

Local<Number> Number::newNumber(int32_t value) { return Local<Number>(py::int_(value)); }

Local<Number> Number::newNumber(int64_t value) { return Local<Number>(py::int_(value)); }

Local<Boolean> Boolean::newBoolean(bool value) { return Local<Boolean>(py::bool_(value)); }

namespace {

static constexpr const char* kFunctionDataName = "_ScriptX_function_data";

struct FunctionData {
  FunctionCallback function;
  py_backend::PyEngine* engine = nullptr;
};

}  // namespace

Local<Function> Function::newFunction(script::FunctionCallback callback) {
  py::cpp_function func = [callback](py::args args) {
    return py_interop::toPy(
        callback(py_interop::makeArguments(&py_backend::currentEngine(), py::dict(), args)));
  };
  return Local<Function>(func);
}

Local<Array> Array::newArray(size_t size) { return Local<Array>(py::list(size)); }

Local<Array> Array::newArrayImpl(size_t size, const Local<Value>* args) {
  py::list list(size);
  for (size_t i = 0; i < size; ++i) {
    list[i] = Local<Value>(args[i]);
  }
  return Local<Array>(list);
}

Local<ByteBuffer> ByteBuffer::newByteBuffer(size_t size) {
  return Local<ByteBuffer>(py::bytearray());
}

Local<script::ByteBuffer> ByteBuffer::newByteBuffer(void* nativeBuffer, size_t size) {
  return Local<script::ByteBuffer>(
      py::bytearray(reinterpret_cast<const char*>(nativeBuffer), size));
}

Local<ByteBuffer> ByteBuffer::newByteBuffer(std::shared_ptr<void> nativeBuffer, size_t size) {
  return Local<ByteBuffer>(py::bytearray(reinterpret_cast<const char*>(nativeBuffer.get()), size));
}

}  // namespace script