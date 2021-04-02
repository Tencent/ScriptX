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
#include "QjsHelper.hpp"

namespace script {

namespace qjs_backend {

struct GlobalRefState {
  JSValue ref_ = JS_UNDEFINED;
  QjsEngine* engine_ = nullptr;
  internal::GlobalWeakBookkeeping::HandleType handle_{};
};

struct WeakRefState : GlobalRefState {};

struct QjsEngine::BookKeepFetcher {
  template <typename T>
  static ::script::internal::GlobalWeakBookkeeping* get(const T* ref) {
    if (!ref) return nullptr;
    auto& val = ref->val_;
    if (!val.engine_) return nullptr;
    return &val.engine_->globalWeakBookkeeping_;
  }

  template <typename T>
  static ::script::internal::GlobalWeakBookkeeping::HandleType& handle(const T* ref) {
    auto& val = const_cast<T*>(ref)->val_;
    return val.handle_;
  }
};

struct QjsBookKeepFetcher : QjsEngine::BookKeepFetcher {};

using BookKeep = ::script::internal::GlobalWeakBookkeeping::Helper<QjsBookKeepFetcher>;

}  // namespace qjs_backend

template <typename T>
Global<T>::Global() noexcept : val_() {}

template <typename T>
Global<T>::Global(const script::Local<T>& localReference) {
  if (!localReference.isNull()) {
    val_.ref_ = localReference.val_;
    val_.engine_ = &qjs_backend::currentEngine();
    qjs_backend::dupValue(val_.ref_, val_.engine_->context_);

    qjs_backend::BookKeep::keep(this);
  }
}

template <typename T>
Global<T>::Global(const script::Weak<T>& weak) {}

template <typename T>
Global<T>::Global(const script::Global<T>& copy) : val_() {
  *this = copy;
}

template <typename T>
Global<T>::Global(script::Global<T>&& move) noexcept : val_() {
  *this = std::move(move);
}

template <typename T>
Global<T>::~Global() {
  if (!isEmpty()) {
    EngineScope scope(val_.engine_);
    reset();
  }
}

template <typename T>
Global<T>& Global<T>::operator=(const script::Global<T>& assign) {
  bool wasEmpty = isEmpty();
  if (!wasEmpty) {
    qjs_backend::freeValue(val_.ref_, val_.engine_->context_);
  }

  val_.ref_ = assign.val_.ref_;
  val_.engine_ = assign.val_.engine_;
  if (val_.engine_) qjs_backend::dupValue(val_.ref_, val_.engine_->context_);

  qjs_backend::BookKeep::afterCopy(wasEmpty, this, &assign);
  return *this;
}

template <typename T>
Global<T>& Global<T>::operator=(script::Global<T>&& move) noexcept {
  bool wasEmpty = isEmpty();
  if (!wasEmpty) {
    qjs_backend::freeValue(val_.ref_, val_.engine_->context_);
  }

  val_.ref_ = move.val_.ref_;
  val_.engine_ = move.val_.engine_;
  move.val_.ref_ = JS_UNDEFINED;
  move.val_.engine_ = nullptr;

  qjs_backend::BookKeep::afterMove(wasEmpty, this, &move);
  return *this;
}

template <typename T>
void Global<T>::swap(Global& rhs) noexcept {
  std::swap(val_.ref_, rhs.val_.ref_);
  std::swap(val_.engine_, rhs.val_.engine_);
  qjs_backend::BookKeep::afterSwap(this, &rhs);
}

template <typename T>
Global<T>& Global<T>::operator=(const script::Local<T>& assign) {
  *this = Global<T>(assign);
  return *this;
}

template <typename T>
Local<T> Global<T>::get() const {
  if (isEmpty()) throw Exception("get on empty Global");
  return qjs_interop::makeLocal<T>(qjs_backend::dupValue(val_.ref_, val_.engine_->context_));
}

template <typename T>
Local<Value> Global<T>::getValue() const {
  if (isEmpty()) return {};
  return qjs_interop::makeLocal<Value>(qjs_backend::dupValue(val_.ref_, val_.engine_->context_));
}

template <typename T>
bool Global<T>::isEmpty() const {
  return val_.engine_ == nullptr;
}

template <typename T>
void Global<T>::reset() {
  if (!isEmpty()) {
    qjs_backend::freeValue(val_.ref_, val_.engine_->context_);
    qjs_backend::BookKeep::remove(this);
    val_.ref_ = JS_UNDEFINED;
    val_.engine_ = nullptr;
  }
}

// == Weak ==

template <typename T>
Weak<T>::Weak() noexcept : val_() {}

template <typename T>
Weak<T>::~Weak() {}

template <typename T>
Weak<T>::Weak(const script::Local<T>& localReference) {}

template <typename T>
Weak<T>::Weak(const script::Global<T>& globalReference) {}

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
  TEMPLATE_NOT_IMPLEMENTED();
}

template <typename T>
Local<Value> Weak<T>::getValue() const {
  TEMPLATE_NOT_IMPLEMENTED();
}

template <typename T>
bool Weak<T>::isEmpty() const {
  return false;
}

template <typename T>
void Weak<T>::reset() noexcept {}

}  // namespace script
