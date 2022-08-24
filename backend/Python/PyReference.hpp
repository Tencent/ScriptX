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
#include <utility>
#include "PyHelper.hpp"

namespace script {

template <typename T>
Global<T>::Global() noexcept : val_(nullptr) {}

template <typename T>
Global<T>::Global(const script::Local<T>& localReference)
    : val_(py_backend::incRef(localReference.val_)) {}

template <typename T>
Global<T>::Global(const script::Weak<T>& weak) : val_(py_backend::incRef(weak.val_)) {}

template <typename T>
Global<T>::Global(const script::Global<T>& copy) : val_(copy.val_) {}

template <typename T>
Global<T>::Global(script::Global<T>&& move) noexcept : val_(std::move(move.val_)) {}

template <typename T>
Global<T>::~Global() {}

template <typename T>
Global<T>& Global<T>::operator=(const script::Global<T>& assign) {
  Global(assign).swap(*this);
  return *this;
}

template <typename T>
Global<T>& Global<T>::operator=(script::Global<T>&& move) noexcept {
  Global(std::move(move)).swap(*this);
  return *this;
}

template <typename T>
void Global<T>::swap(Global& rhs) noexcept {
  std::swap(val_, rhs.val_);
}

template <typename T>
Global<T>& Global<T>::operator=(const script::Local<T>& assign) {
  *this = Global<T>(assign);
  return *this;
}

template <typename T>
Local<T> Global<T>::get() const {
  return py_interop::toLocal<T>(val_);
}

template <typename T>
Local<Value> Global<T>::getValue() const {
  return py_interop::toLocal<Value>(val_);
}

template <typename T>
bool Global<T>::isEmpty() const {
  return val_ == nullptr;
}

template <typename T>
void Global<T>::reset() {
  py_backend::decRef(val_);
  val_ = nullptr;
}

// == Weak ==

template <typename T>
Weak<T>::Weak() noexcept : val_() {}

template <typename T>
Weak<T>::~Weak() {
  val_ = nullptr;
}

template <typename T>
Weak<T>::Weak(const script::Local<T>& localReference) : val_(localReference.val_) {}

template <typename T>
Weak<T>::Weak(const script::Global<T>& globalReference) : val_(globalReference.val_) {}

template <typename T>
Weak<T>::Weak(const script::Weak<T>& copy) : val_(copy.val_) {}

template <typename T>
Weak<T>::Weak(script::Weak<T>&& move) noexcept : val_(std::move(move.val_)) {}

template <typename T>
Weak<T>& Weak<T>::operator=(const script::Weak<T>& assign) {
  val_ = assign.val_;
  return *this;
}

template <typename T>
Weak<T>& Weak<T>::operator=(script::Weak<T>&& move) noexcept {
  val_ = std::move(move.val_);
  return *this;
}

template <typename T>
void Weak<T>::swap(Weak& rhs) noexcept {
  std::swap(val_, rhs.val_);
}

template <typename T>
Weak<T>& Weak<T>::operator=(const script::Local<T>& assign) {
  *this = Weak<T>(assign);
  return *this;
}

template <typename T>
Local<T> Weak<T>::get() const {
  if (isEmpty()) throw Exception("get on empty Weak");
  return Local<T>(val_);
}

template <typename T>
Local<Value> Weak<T>::getValue() const {
  if (isEmpty()) throw Exception("getValue on empty Weak");
  return Local<Value>(val_);
}

template <typename T>
bool Weak<T>::isEmpty() const {
  return val_ == nullptr;
}

template <typename T>
void Weak<T>::reset() noexcept {
  val_ = nullptr;
}

}  // namespace script
