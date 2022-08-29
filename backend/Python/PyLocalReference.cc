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

#include "../../src/Native.hpp"
#include "../../src/Reference.h"
#include "../../src/Utils.h"
#include "../../src/Value.h"
#include "PyHelper.hpp"
#include "PyReference.hpp"

namespace script {

namespace py_backend {
void valueConstructorCheck(PyObject* value) {
  SCRIPTX_UNUSED(value);
#ifndef NDEBUG
  if (!value) throw Exception("null reference");
#endif
}
}  // namespace py_backend

#define REF_IMPL_BASIC_FUNC(ValueType)                                                           \
  Local<ValueType>::Local(const Local<ValueType>& copy) : val_(py_backend::incRef(copy.val_)) {} \
  Local<ValueType>::Local(Local<ValueType>&& move) noexcept : val_(move.val_) {                  \
    move.val_ = nullptr;                                                                         \
  }                                                                                              \
  Local<ValueType>::~Local() { py_backend::decRef(val_); }                                       \
  Local<ValueType>& Local<ValueType>::operator=(const Local& from) {                             \
    Local(from).swap(*this);                                                                     \
    return *this;                                                                                \
  }                                                                                              \
  Local<ValueType>& Local<ValueType>::operator=(Local&& move) noexcept {                         \
    Local(std::move(move)).swap(*this);                                                          \
    return *this;                                                                                \
  }                                                                                              \
  void Local<ValueType>::swap(Local& rhs) noexcept { std::swap(val_, rhs.val_); }

#define REF_IMPL_BASIC_EQUALS(ValueType)                                               \
  bool Local<ValueType>::operator==(const script::Local<script::Value>& other) const { \
    return asValue() == other;                                                         \
  }

#define REF_IMPL_BASIC_NOT_VALUE(ValueType)                                         \
  Local<ValueType>::Local(InternalLocalRef val) : val_(py_backend::incRef(val)) {   \
    py_backend::valueConstructorCheck(val);                                         \
  }                                                                                 \
  Local<String> Local<ValueType>::describe() const { return asValue().describe(); } \
  std::string Local<ValueType>::describeUtf8() const { return asValue().describeUtf8(); }

#define REF_IMPL_TO_VALUE(ValueType) \
  Local<Value> Local<ValueType>::asValue() const { return Local<Value>(py_backend::incRef(val_)); }

REF_IMPL_BASIC_FUNC(Value)

REF_IMPL_BASIC_FUNC(Object)
REF_IMPL_BASIC_NOT_VALUE(Object)
REF_IMPL_BASIC_EQUALS(Object)
REF_IMPL_TO_VALUE(Object)

REF_IMPL_BASIC_FUNC(String)
REF_IMPL_BASIC_NOT_VALUE(String)
REF_IMPL_BASIC_EQUALS(String)
REF_IMPL_TO_VALUE(String)

REF_IMPL_BASIC_FUNC(Number)
REF_IMPL_BASIC_NOT_VALUE(Number)
REF_IMPL_BASIC_EQUALS(Number)
REF_IMPL_TO_VALUE(Number)

REF_IMPL_BASIC_FUNC(Boolean)
REF_IMPL_BASIC_NOT_VALUE(Boolean)
REF_IMPL_BASIC_EQUALS(Boolean)
REF_IMPL_TO_VALUE(Boolean)

REF_IMPL_BASIC_FUNC(Function)
REF_IMPL_BASIC_NOT_VALUE(Function)
REF_IMPL_BASIC_EQUALS(Function)
REF_IMPL_TO_VALUE(Function)

REF_IMPL_BASIC_FUNC(Array)
REF_IMPL_BASIC_NOT_VALUE(Array)
REF_IMPL_BASIC_EQUALS(Array)
REF_IMPL_TO_VALUE(Array)

REF_IMPL_BASIC_FUNC(ByteBuffer)
REF_IMPL_BASIC_NOT_VALUE(ByteBuffer)
REF_IMPL_BASIC_EQUALS(ByteBuffer)
REF_IMPL_TO_VALUE(ByteBuffer)

REF_IMPL_BASIC_FUNC(Unsupported)
REF_IMPL_BASIC_NOT_VALUE(Unsupported)
REF_IMPL_BASIC_EQUALS(Unsupported)
REF_IMPL_TO_VALUE(Unsupported)

// ==== value ====

Local<Value>::Local() noexcept : val_(py_backend::incRef(Py_None)) {}

Local<Value>::Local(InternalLocalRef ref) : val_(ref) {
  if (ref == nullptr) throw Exception("Python exception occurred!");
}

bool Local<Value>::isNull() const { return Py_IsNone(val_); }

void Local<Value>::reset() {
  py_backend::decRef(val_);
  val_ = nullptr;
}

ValueKind Local<Value>::getKind() const {
  if (isNull()) {
    return ValueKind::kNull;
  } else if (isString()) {
    return ValueKind::kString;
  } else if (isNumber()) {
    return ValueKind::kNumber;
  } else if (isBoolean()) {
    return ValueKind::kBoolean;
  } else if (isFunction()) {
    return ValueKind::kFunction;
  } else if (isArray()) {
    return ValueKind::kArray;
  } else if (isByteBuffer()) {
    return ValueKind::kByteBuffer;
  } else if (isObject()) {
    return ValueKind::kObject;
  } else {
    return ValueKind::kUnsupported;
  }
}

bool Local<Value>::isString() const { return PyUnicode_Check(val_); }

bool Local<Value>::isNumber() const { return PyNumber_Check(val_); }

bool Local<Value>::isBoolean() const { return PyBool_Check(val_); }

bool Local<Value>::isFunction() const { return PyFunction_Check(val_) || PyCFunction_Check(val_); }

bool Local<Value>::isArray() const { return PyList_Check(val_); }

bool Local<Value>::isByteBuffer() const { return PyBytes_Check(val_); }

bool Local<Value>::isObject() const { return PyDict_Check(val_); }

bool Local<Value>::isUnsupported() const { return true; }

Local<String> Local<Value>::asString() const {
  if (isString()) return Local<String>(val_);
  throw Exception("can't cast value as String");
}

Local<Number> Local<Value>::asNumber() const {
  if (isNumber()) return Local<Number>(val_);
  throw Exception("can't cast value as Number");
}

Local<Boolean> Local<Value>::asBoolean() const {
  if (isBoolean()) return Local<Boolean>(val_);
  throw Exception("can't cast value as Boolean");
}

Local<Function> Local<Value>::asFunction() const {
  if (isFunction()) return Local<Function>(val_);
  throw Exception("can't cast value as Function");
}

Local<Array> Local<Value>::asArray() const {
  if (isArray()) return Local<Array>(val_);
  throw Exception("can't cast value as Array");
}

Local<ByteBuffer> Local<Value>::asByteBuffer() const {
  if (isByteBuffer()) return Local<ByteBuffer>(val_);
  throw Exception("can't cast value as ByteBuffer");
}

Local<Object> Local<Value>::asObject() const {
  if (isObject()) return Local<Object>(val_);
  throw Exception("can't cast value as Object");
}

Local<Unsupported> Local<Value>::asUnsupported() const {
  throw Exception("can't cast value as Unsupported");
}

bool Local<Value>::operator==(const script::Local<script::Value>& other) const {
  return PyObject_RichCompareBool(val_, other.val_, Py_EQ);
}

Local<String> Local<Value>::describe() const { return Local<String>(PyObject_Str(val_)); }

Local<Value> Local<Object>::get(const script::Local<script::String>& key) const {
  PyObject* item = PyDict_GetItem(val_, key.val_);
  if (item)
    return py_interop::toLocal<Value>(item);
  else
    return Local<Value>();
}

void Local<Object>::set(const script::Local<script::String>& key,
                        const script::Local<script::Value>& value) const {
  PyDict_SetItem(val_, key.val_, value.val_);
}

void Local<Object>::remove(const Local<class script::String>& key) const {
  PyDict_DelItem(val_, key.val_);
}

bool Local<Object>::has(const Local<class script::String>& key) const {
  return PyDict_Contains(val_, key.val_);
}

bool Local<Object>::instanceOf(const Local<class script::Value>& type) const {
  return PyObject_IsInstance(val_, type.val_);
}

std::vector<Local<String>> Local<Object>::getKeys() const {
  std::vector<Local<String>> keys;
  PyObject* key;
  PyObject* value;
  Py_ssize_t pos = 0;
  while (PyDict_Next(val_, &pos, &key, &value)) {
    keys.push_back(Local<String>(key));
  }
  return keys;
}

float Local<Number>::toFloat() const { return static_cast<float>(toDouble()); }

double Local<Number>::toDouble() const { return PyFloat_AsDouble(val_); }

int32_t Local<Number>::toInt32() const { return static_cast<int32_t>(toDouble()); }

int64_t Local<Number>::toInt64() const { return static_cast<int64_t>(toDouble()); }

bool Local<Boolean>::value() const { return Py_IsTrue(val_); }

Local<Value> Local<Function>::callImpl(const Local<Value>& thiz, size_t size,
                                       const Local<Value>* args) const {
  PyObject* args_tuple = PyTuple_New(size);
  for (size_t i = 0; i < size; ++i) {
    PyTuple_SetItem(args_tuple, i, args[i].val_);
  }
  PyObject* result = PyObject_CallObject(val_, args_tuple);
  py_backend::decRef(args_tuple);
  return py_interop::asLocal<Value>(result);
}

size_t Local<Array>::size() const { return PyList_Size(val_); }

Local<Value> Local<Array>::get(size_t index) const {
  PyObject* item = PyList_GetItem(val_, index);
  if (item)
    return py_interop::toLocal<Value>(item);
  else
    return Local<Value>();
}

void Local<Array>::set(size_t index, const script::Local<script::Value>& value) const {
  size_t listSize = size();
  if (index >= listSize)
    for (size_t i = listSize; i <= index; ++i) {
      PyList_Append(val_, Py_None);
    }
  PyList_SetItem(val_, index, py_interop::getPy(value));
}

void Local<Array>::add(const script::Local<script::Value>& value) const {
  PyList_Append(val_, py_interop::peekPy(value));
}

void Local<Array>::clear() const { PyList_SetSlice(val_, 0, PyList_Size(val_), nullptr); }

ByteBuffer::Type Local<ByteBuffer>::getType() const { return ByteBuffer::Type::kInt8; }

bool Local<ByteBuffer>::isShared() const { return false; }

void Local<ByteBuffer>::commit() const {}

void Local<ByteBuffer>::sync() const {}

size_t Local<ByteBuffer>::byteLength() const { return PyBytes_Size(val_); }

void* Local<ByteBuffer>::getRawBytes() const { return PyBytes_AsString(val_); }

std::shared_ptr<void> Local<ByteBuffer>::getRawBytesShared() const { return nullptr; }

}  // namespace script
