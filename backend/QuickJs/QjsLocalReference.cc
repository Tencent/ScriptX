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

#include <ScriptX/ScriptX.h>

namespace script {

#define REF_IMPL_BASIC_FUNC(ValueType)                                          \
  Local<ValueType>::Local(const Local<ValueType>& copy) : val_(copy.val_) {     \
    qjs_backend::dupValue(val_);                                                \
  }                                                                             \
  Local<ValueType>::Local(Local<ValueType>&& move) noexcept : val_(move.val_) { \
    move.val_ = JS_UNDEFINED;                                                   \
  }                                                                             \
  Local<ValueType>::~Local() { qjs_backend::freeValue(val_); }                  \
  Local<ValueType>& Local<ValueType>::operator=(const Local& from) {            \
    Local(from).swap(*this);                                                    \
    return *this;                                                               \
  }                                                                             \
  Local<ValueType>& Local<ValueType>::operator=(Local&& move) noexcept {        \
    Local(std::move(move)).swap(*this);                                         \
    return *this;                                                               \
  }                                                                             \
  void Local<ValueType>::swap(Local& rhs) noexcept { std::swap(val_, rhs.val_); }

#define REF_IMPL_BASIC_EQUALS(ValueType)                                               \
  bool Local<ValueType>::operator==(const script::Local<script::Value>& other) const { \
    return asValue() == other;                                                         \
  }

#define REF_IMPL_BASIC_NOT_VALUE(ValueType)                                         \
  Local<ValueType>::Local(InternalLocalRef val) : val_(val) {}                      \
  Local<String> Local<ValueType>::describe() const { return asValue().describe(); } \
  std::string Local<ValueType>::describeUtf8() const { return asValue().describeUtf8(); }

#define REF_IMPL_TO_VALUE(ValueType)                  \
  Local<Value> Local<ValueType>::asValue() const {    \
    return Local<Value>(qjs_backend::dupValue(val_)); \
  }

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

Local<Value>::Local() noexcept : val_(JS_UNDEFINED) {}

Local<Value>::Local(InternalLocalRef local) : val_(local) {}

bool Local<Value>::isNull() const {
  return JS_IsNull(val_) || JS_IsUninitialized(val_) || JS_IsUndefined(val_);
}

void Local<Value>::reset() { *this = Local<Value>(); }

ValueKind Local<Value>::getKind() const {
  if (isNull()) {
    return ValueKind::kNull;
  } else if (isString()) {
    return ValueKind::kString;
  } else if (isNumber()) {
    return ValueKind::kNumber;
  } else if (isBoolean()) {
    return ValueKind::kBoolean;
  }

  auto context = qjs_backend::currentContext();

  if (JS_IsFunction(context, val_)) {
    return ValueKind::kFunction;
  } else if (JS_IsArray(context, val_)) {
    return ValueKind::kArray;
  } else if (isByteBuffer()) {
    return ValueKind::kByteBuffer;
  } else if (isObject()) {
    return ValueKind::kObject;
  } else {
    return ValueKind::kUnsupported;
  }
}

bool Local<Value>::isString() const { return JS_IsString(val_); }

bool Local<Value>::isNumber() const { return JS_IsNumber(val_); }

bool Local<Value>::isBoolean() const { return JS_IsBool(val_); }

bool Local<Value>::isFunction() const { return JS_IsFunction(qjs_backend::currentContext(), val_); }

bool Local<Value>::isArray() const { return JS_IsArray(qjs_backend::currentContext(), val_); }

bool Local<Value>::isByteBuffer() const {
  TEMPLATE_NOT_IMPLEMENTED();
  return false;
}

bool Local<Value>::isObject() const { return JS_IsObject(val_); }

bool Local<Value>::isUnsupported() const { return getKind() == ValueKind::kUnsupported; }

Local<String> Local<Value>::asString() const {
  if (isString()) return Local<String>(qjs_backend::dupValue(val_));
  throw Exception(u8"can't cast value as String");
}

Local<Number> Local<Value>::asNumber() const {
  if (isNumber()) return Local<Number>(qjs_backend::dupValue(val_));
  throw Exception(u8"can't cast value as Number");
}

Local<Boolean> Local<Value>::asBoolean() const {
  if (isBoolean()) return Local<Boolean>(qjs_backend::dupValue(val_));
  throw Exception(u8"can't cast value as Boolean");
}

Local<Function> Local<Value>::asFunction() const {
  if (isFunction()) return Local<Function>(qjs_backend::dupValue(val_));
  throw Exception(u8"can't cast value as Function");
}

Local<Unsupported> Local<Value>::asUnsupported() const {
  if (isUnsupported()) return Local<Unsupported>(qjs_backend::dupValue(val_));
  throw Exception(u8"can't cast value as Unsupported");
}

Local<Array> Local<Value>::asArray() const {
  if (isArray()) return Local<Array>(qjs_backend::dupValue(val_));
  throw Exception("can't cast value as Array");
}

Local<ByteBuffer> Local<Value>::asByteBuffer() const {
  if (isByteBuffer()) return Local<ByteBuffer>(qjs_backend::dupValue(val_));
  throw Exception("can't cast value as ByteBuffer");
}

Local<Object> Local<Value>::asObject() const {
  if (isObject()) return Local<Object>(qjs_backend::dupValue(val_));
  throw Exception("can't cast value as Object");
}

bool Local<Value>::operator==(const script::Local<script::Value>& other) const {
  if (isNull()) return other.isNull();
  TEMPLATE_NOT_IMPLEMENTED();
  return false;
}

Local<String> Local<Value>::describe() const {
  auto ret = JS_ToString(qjs_backend::currentContext(), val_);
  qjs_backend::checkException(ret);
  return Local<String>(ret);
}

Local<Value> Local<Object>::get(const script::Local<script::String>& key) const { return {}; }

void Local<Object>::set(const script::Local<script::String>& key,
                        const script::Local<script::Value>& value) const {}

void Local<Object>::remove(const Local<class script::String>& key) const {}
bool Local<Object>::has(const Local<class script::String>& key) const { return true; }

bool Local<Object>::instanceOf(const Local<class script::Value>& type) const { return false; }

std::vector<Local<String>> Local<Object>::getKeys() const { return {}; }

float Local<Number>::toFloat() const { return static_cast<float>(toDouble()); }

double Local<Number>::toDouble() const {
  double ret = 0;
  qjs_backend::checkException(JS_ToFloat64(qjs_backend::currentContext(), &ret, val_));
  return ret;
}

int32_t Local<Number>::toInt32() const {
  int32_t ret = 0;
  qjs_backend::checkException(JS_ToInt32(qjs_backend::currentContext(), &ret, val_));
  return ret;
}

int64_t Local<Number>::toInt64() const {
  int64_t ret = 0;
  qjs_backend::checkException(JS_ToInt64(qjs_backend::currentContext(), &ret, val_));
  return ret;
}

bool Local<Boolean>::value() const { return JS_ToBool(qjs_backend::currentContext(), val_) != 0; }

Local<Value> Local<Function>::callImpl(const Local<Value>& thiz, size_t size,
                                       const Local<Value>* args) const {
  return {};
}

size_t Local<Array>::size() const {
  auto& engine = qjs_backend::currentEngine();
  // length
  uint32_t size = 0;
  auto length = JS_GetProperty(engine.context_, val_, engine.lengthAtom_);
  qjs_backend::checkException(length);
  if (JS_IsNumber(length)) {
    JS_ToUint32(engine.context_, &size, length);
    JS_FreeValue(engine.context_, length);
  } else {
    JS_FreeValue(engine.context_, length);
    qjs_backend::checkException(-1, "Local<Array>::size got not a number");
  }

  return size;
}

Local<Value> Local<Array>::get(size_t index) const {
  // own
  auto ret =
      JS_GetPropertyUint32(qjs_backend::currentContext(), val_, static_cast<uint32_t>(index));
  qjs_backend::checkException(ret);
  return qjs_interop::makeLocal<Value>(ret);
}

void Local<Array>::set(size_t index, const script::Local<script::Value>& value) const {
  qjs_backend::checkException(JS_SetPropertyInt64(qjs_backend::currentContext(), val_,
                                                  static_cast<int64_t>(index),
                                                  qjs_interop::getLocal(value)));
}

void Local<Array>::add(const script::Local<script::Value>& value) const { set(size(), value); }

void Local<Array>::clear() const {
  auto& engine = qjs_backend::currentEngine();

  auto number = JS_NewUint32(engine.context_, static_cast<uint32_t>(0));
  qjs_backend::checkException(JS_SetProperty(engine.context_, val_, engine.lengthAtom_, number));
}

ByteBuffer::Type Local<ByteBuffer>::getType() const { return ByteBuffer::Type::KFloat32; }

bool Local<ByteBuffer>::isShared() const { return true; }

void Local<ByteBuffer>::commit() const {}

void Local<ByteBuffer>::sync() const {}

size_t Local<ByteBuffer>::byteLength() const { return 0; }

void* Local<ByteBuffer>::getRawBytes() const { return nullptr; }

std::shared_ptr<void> Local<ByteBuffer>::getRawBytesShared() const {
  return std::shared_ptr<void>(
      getRawBytes(), [val = qjs_backend::dupValue(val_), context = qjs_backend::currentContext()](
                         void* ptr) { JS_FreeValue(context, val); });
}

}  // namespace script