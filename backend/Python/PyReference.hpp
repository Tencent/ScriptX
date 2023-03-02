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
#include "PyEngine.h"
#include <set>

namespace script {

namespace py_backend {
// =============== Refkeepers Helper ===============
// keep or remove refs from ref keeper
// isCreate: 1 create 0 destroy
inline void _updateRefStateInKeeper(GlobalRefState* ref, bool isCreate, bool isEmptyRef)
{
  PyEngine* engine = EngineScope::currentEngineAs<PyEngine>();
  if(!engine)
    return;

  if(isCreate)
  {
    if(!isEmptyRef)
      engine->refsKeeper.keep(ref);
    else
      engine->refsKeeper.remove(ref);    // empty refs is not tracked in ref keeper
  }
  else
    engine->refsKeeper.remove(ref);
}

inline void _updateRefStateInKeeper(WeakRefState* ref, bool isCreate, bool isEmptyRef)    
{
  PyEngine* engine = EngineScope::currentEngineAs<PyEngine>();
  if(!engine)
    return;

  if(isCreate)
  {
    if(!isEmptyRef)
      engine->refsKeeper.keep(ref);
    else
      engine->refsKeeper.remove(ref);    // empty refs is not tracked in ref keeper
  }
  else
    engine->refsKeeper.remove(ref);
}

} // namespace py_backend

// =============== Global ===============

namespace py_backend {

inline GlobalRefState::GlobalRefState(PyObject* obj) 
  :_ref(Py_IsNone(obj) ? Py_None : Py_NewRef(obj)) {}

inline GlobalRefState::GlobalRefState(const GlobalRefState& assign)
  :_ref(assign.isEmpty() ? Py_None : Py_NewRef(assign._ref)) {}

inline GlobalRefState::GlobalRefState(GlobalRefState&& move) noexcept
  : _ref(move._ref)
{
  move._ref = Py_None;
}

inline GlobalRefState& GlobalRefState::operator=(const GlobalRefState& assign){
  if(!isEmpty())
    reset();
  if(!assign.isEmpty())
    _ref = Py_NewRef(assign._ref);
  return *this;
}

inline GlobalRefState& GlobalRefState::operator=(GlobalRefState&& move) noexcept{
  if(!isEmpty())
    reset();

  _ref = move._ref;
  move._ref = Py_None;
  return *this;
}

inline void GlobalRefState::swap(GlobalRefState& other){
  std::swap(_ref, other._ref);
}

inline bool GlobalRefState::isEmpty() const {
  return Py_IsNone(_ref) || _ref == nullptr;
}

inline PyObject *GlobalRefState::get() const {
    return (isEmpty() ? Py_None : Py_NewRef(_ref));
}

inline PyObject *GlobalRefState::peek() const{
    return _ref;
}

inline void GlobalRefState::reset() {
  _ref = Py_None;
}

inline void GlobalRefState::dtor() {
  reset();
}

}   // namespace py_backend

template <typename T>
Global<T>::Global() noexcept : val_(Py_None) {}   // empty refs is not tracked in ref keeper

template <typename T>
Global<T>::Global(const script::Local<T>& localReference) 
  :val_(py_interop::peekPy(localReference))
{
  py_backend::_updateRefStateInKeeper(&val_, true, isEmpty());
}

template <typename T>
Global<T>::Global(const script::Weak<T>& weak) : val_(weak.val_.peek()) {
  py_backend::_updateRefStateInKeeper(&val_, true, isEmpty());
}

template <typename T>
Global<T>::Global(const script::Global<T>& copy) : val_(copy.val_) {
  py_backend::_updateRefStateInKeeper(&val_, true, isEmpty());
}

template <typename T>
Global<T>::Global(script::Global<T>&& move) noexcept : val_(std::move(move.val_)) {
  py_backend::_updateRefStateInKeeper(&val_, true, isEmpty());
  py_backend::_updateRefStateInKeeper(&move.val_, true, true);
}

template <typename T>
Global<T>::~Global() {
  val_.dtor();
  py_backend::_updateRefStateInKeeper(&val_, false, true);
}

template <typename T>
Global<T>& Global<T>::operator=(const script::Global<T>& assign) {
  val_ = assign.val_;
  py_backend::_updateRefStateInKeeper(&val_, true, isEmpty());
  return *this;
}

template <typename T>
Global<T>& Global<T>::operator=(script::Global<T>&& move) noexcept {
  val_ = std::move(move.val_);
  py_backend::_updateRefStateInKeeper(&val_, true, isEmpty());
  py_backend::_updateRefStateInKeeper(&move.val_, true, true);
  return *this;
}

template <typename T>
Global<T>& Global<T>::operator=(const script::Local<T>& assign) {
  val_ = py_backend::GlobalRefState(py_interop::peekPy(assign));
  py_backend::_updateRefStateInKeeper(&val_, true, isEmpty());
  return *this;
}


template <typename T>
void Global<T>::swap(Global& rhs) noexcept {
  val_.swap(rhs.val_);
  py_backend::_updateRefStateInKeeper(&val_, true, isEmpty());
  py_backend::_updateRefStateInKeeper(&rhs.val_, true, rhs.isEmpty());
}

template <typename T>
Local<T> Global<T>::get() const {
  return py_interop::asLocal<T>(val_.get());
}

template <typename T>
Local<Value> Global<T>::getValue() const {
  return py_interop::asLocal<Value>(val_.get());
}

template <typename T>
bool Global<T>::isEmpty() const {
  return val_.isEmpty();
}

template <typename T>
void Global<T>::reset() {
  val_.reset();
  py_backend::_updateRefStateInKeeper(&val_, false, true);
}

// =============== Weak ===============

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
  // if this is not a real ref need to dec ref count
  if(!_isRealWeakRef && !Py_IsNone(_ref))
  {
    Py_XDECREF(_ref);
  }
  _ref = Py_None;
  _isRealWeakRef = false;
}

inline void WeakRefState::dtor() {
  reset();
}

}   // namespace py_backend

template <typename T>
Weak<T>::Weak() noexcept {};   // empty refs is not tracked in ref keeper

template <typename T>
Weak<T>::~Weak() {
  val_.dtor();
  py_backend::_updateRefStateInKeeper(&val_, false, true);
}

template <typename T>
Weak<T>::Weak(const script::Local<T>& localReference) 
  : val_(py_interop::peekPy(localReference))
{
  py_backend::_updateRefStateInKeeper(&val_, true, val_.isEmpty());
}

template <typename T>
Weak<T>::Weak(const script::Global<T>& globalReference) : val_(globalReference.val_.peek()) {
  py_backend::_updateRefStateInKeeper(&val_, true, val_.isEmpty());
}

template <typename T>
Weak<T>::Weak(const script::Weak<T>& copy) : val_(copy.val_) {
  py_backend::_updateRefStateInKeeper(&val_, true, val_.isEmpty());
}

template <typename T>
Weak<T>::Weak(script::Weak<T>&& move) noexcept : val_(std::move(move.val_)) {
  py_backend::_updateRefStateInKeeper(&val_, true, val_.isEmpty());
  py_backend::_updateRefStateInKeeper(&move.val_, true, true);
}

template <typename T>
Weak<T>& Weak<T>::operator=(const script::Weak<T>& assign) {
  val_ = assign.val_;
  py_backend::_updateRefStateInKeeper(&val_, true, val_.isEmpty());
  return *this;
}

template <typename T>
Weak<T>& Weak<T>::operator=(script::Weak<T>&& move) noexcept {
  val_ = std::move(move.val_);
  py_backend::_updateRefStateInKeeper(&val_, true, val_.isEmpty());
  py_backend::_updateRefStateInKeeper(&move.val_, true, true);
  return *this;
}

template <typename T>
Weak<T>& Weak<T>::operator=(const script::Local<T>& assign) {
  val_ = py_backend::WeakRefState(py_interop::peekPy(assign));
  py_backend::_updateRefStateInKeeper(&val_, true, val_.isEmpty());
  return *this;
}

template <typename T>
void Weak<T>::swap(Weak& rhs) noexcept {
  val_.swap(rhs.val_);
  py_backend::_updateRefStateInKeeper(&val_, true, val_.isEmpty());
  py_backend::_updateRefStateInKeeper(&rhs.val_, true, rhs.val_.isEmpty());
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
  py_backend::_updateRefStateInKeeper(&val_, false, true);
}

}  // namespace script
