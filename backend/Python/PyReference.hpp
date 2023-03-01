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
Global<T>::Global() noexcept : val_(Py_None) {}

template <typename T>
Global<T>::Global(const script::Local<T>& localReference) 
  :val_(py_interop::getPy(localReference)) {}

template <typename T>
Global<T>::Global(const script::Weak<T>& weak) : val_(Py_NewRef(weak.val_.peek())) {}

template <typename T>
Global<T>::Global(const script::Global<T>& copy) : val_(Py_NewRef(copy.val_)) {}

template <typename T>
Global<T>::Global(script::Global<T>&& move) noexcept : val_(std::move(move.val_)) {
  move.val_ = Py_None;
}

template <typename T>
Global<T>::~Global() {
  reset();
}

template <typename T>
Global<T>& Global<T>::operator=(const script::Global<T>& assign) {
  if(!isEmpty())
    reset();
  val_ = Py_NewRef(assign.val_);
  return *this;
}

template <typename T>
Global<T>& Global<T>::operator=(script::Global<T>&& move) noexcept {
  if(!isEmpty())
    reset();
  val_ = std::move(move.val_);
  move.val_ = Py_None;
  return *this;
}

template <typename T>
void Global<T>::swap(Global& rhs) noexcept {
  std::swap(val_, rhs.val_);
}

template <typename T>
Global<T>& Global<T>::operator=(const script::Local<T>& assign) {
  if(!isEmpty())
    reset();
  val_ = Py_NewRef(assign.val_);
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
  return Py_IsNone(val_) || val_ == nullptr;
}

template <typename T>
void Global<T>::reset() {
  Py_XDECREF(val_);
  val_ = Py_None;
}

// == Weak ==

namespace py_backend {

inline WeakRefState::WeakRefState(PyObject* obj) {
  if(Py_IsNone(obj))
    return;

  _ref = PyWeakref_NewRef(obj, NULL);
  if(checkErrorAndClear() || !_ref)
  {
    // Fail to create weak ref, change to global ref
    _isRealWeakRef = false;
    _ref = Py_NewRef(obj);
  }
  else
    _isRealWeakRef = true;
}

inline WeakRefState::WeakRefState(const WeakRefState& assign) {
  if(assign.isEmpty())
    return;
  _isRealWeakRef = assign._isRealWeakRef;
  PyObject *originRef = assign.peek();
  if(_isRealWeakRef)
  {
    _ref = PyWeakref_NewRef(originRef, NULL);
    if(checkErrorAndClear() || !_ref)
    {
      // Fail to create weak ref, change to global ref
      _isRealWeakRef = false;
      _ref = Py_NewRef(originRef);
    }
  }
  else
  {
    // assign is fake wake ref (global ref)
    _isRealWeakRef = false;
    _ref = Py_NewRef(originRef);
  }
}

inline WeakRefState::WeakRefState(WeakRefState&& move) noexcept{
  _isRealWeakRef = move._isRealWeakRef;
  _ref = move._ref;

  move._ref = Py_None;
  move._isRealWeakRef = false;
}

inline WeakRefState& WeakRefState::operator=(const WeakRefState& assign){
  if(!isEmpty())
    reset();
  if(assign.isEmpty())
    return *this;

  _isRealWeakRef = assign._isRealWeakRef;
  PyObject *originRef = assign.peek();
  if(_isRealWeakRef)
  {
    _ref = PyWeakref_NewRef(originRef, NULL);
    if(checkErrorAndClear() || !_ref)
    {
      // Fail to create weak ref, change to global ref
      _isRealWeakRef = false;
      _ref = Py_NewRef(originRef);
    }
  }
  else
  {
    // assign is global ref
    _isRealWeakRef = false;
    _ref = Py_NewRef(originRef);
  }
  return *this;
}

inline WeakRefState& WeakRefState::operator=(WeakRefState&& move) noexcept{
  if(!isEmpty())
    reset();

  _isRealWeakRef = move._isRealWeakRef;
  _ref = move._ref;

  move._ref = Py_None;
  move._isRealWeakRef = false;
  return *this;
}

inline void WeakRefState::swap(WeakRefState& other){
  std::swap(_isRealWeakRef, other._isRealWeakRef);
  std::swap(_ref, other._ref);
}

inline bool WeakRefState::isEmpty() const {
  PyObject *ref = peek();
  return Py_IsNone(ref) || ref == nullptr;
}

inline PyObject *WeakRefState::get() const{
  if(_isRealWeakRef)
  {
    if(Py_IsNone(_ref))
      return Py_None;
    PyObject* obj = PyWeakref_GetObject(_ref);
    return (Py_IsNone(obj) ? Py_None : Py_NewRef(obj));
  }
  else
  {
    // is fake weak ref (global ref)
    return (Py_IsNone(_ref) ? Py_None : Py_NewRef(_ref));
  }
}

inline PyObject *WeakRefState::peek() const{
  if(_isRealWeakRef)
  {
    return (Py_IsNone(_ref) ? Py_None : PyWeakref_GetObject(_ref));
  }
  else
  {
    // is fake weak ref (global ref)
    return _ref;
  }
}

inline bool WeakRefState::isRealWeakRef() const {
  return _isRealWeakRef;
}

inline void WeakRefState::reset() {
  if(!_isRealWeakRef && !Py_IsNone(_ref))
  {
    Py_XDECREF(_ref);
  }
  _ref = Py_None;
  _isRealWeakRef = false;
}

inline void WeakRefState::dtor() {
  // if this is not a real ref need to dec ref count
  if(!_isRealWeakRef && !Py_IsNone(_ref))
  {
    Py_XDECREF(_ref);
  }
}

}   // namespace py_backend

template <typename T>
Weak<T>::Weak() noexcept {};

template <typename T>
Weak<T>::~Weak() {
  val_.dtor();
}

template <typename T>
Weak<T>::Weak(const script::Local<T>& localReference) : val_(py_interop::peekPy(localReference)) {}

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
  val_.swap(rhs.val_);
}

template <typename T>
Weak<T>& Weak<T>::operator=(const script::Local<T>& assign) {
  val_ = py_backend::WeakRefState(py_interop::peekPy(assign));
  return *this;
}

template <typename T>
Local<T> Weak<T>::get() const {
  if (isEmpty()) throw Exception("get on empty Weak");
  return py_interop::asLocal<T>(val_.get());
}

template <typename T>
Local<Value> Weak<T>::getValue() const {
  if (isEmpty()) return Local<Value>();
  return py_interop::asLocal<Value>(val_.get());
}

template <typename T>
bool Weak<T>::isEmpty() const {
  return val_.isEmpty();
}

template <typename T>
void Weak<T>::reset() noexcept {
  val_.reset();
}

}  // namespace script
