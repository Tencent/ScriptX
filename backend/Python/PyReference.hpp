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

// =============== Global ===============

namespace py_backend {

inline GlobalRefState::GlobalRefState() 
  :_ref(Py_NewRef(Py_None)), _engine(EngineScope::currentEngineAs<PyEngine>())
{
  PyEngine::refsKeeper.update(this, _engine);
}

inline GlobalRefState::GlobalRefState(PyObject* obj) 
  :_ref(Py_NewRef(obj)), _engine(EngineScope::currentEngineAs<PyEngine>()) 
{
  PyEngine::refsKeeper.update(this, _engine);
}

inline GlobalRefState::GlobalRefState(const GlobalRefState& assign)
  :_ref(Py_NewRef(assign._ref)), _engine(assign._engine) 
{
  PyEngine::refsKeeper.update(this, _engine);
}

inline GlobalRefState::GlobalRefState(GlobalRefState&& move) noexcept
  : _ref(move._ref), _engine(move._engine)
{
  PyEngine::refsKeeper.update(this, _engine);
  move._ref = Py_NewRef(Py_None);
}

inline GlobalRefState& GlobalRefState::operator=(const GlobalRefState& assign){
  Py_XDECREF(_ref);
  _ref = Py_NewRef(assign._ref);
  _engine = assign._engine;
  PyEngine::refsKeeper.update(this, _engine);
  return *this;
}

inline GlobalRefState& GlobalRefState::operator=(GlobalRefState&& move) noexcept{
  Py_XDECREF(_ref);
  _ref = move._ref;
  _engine = move._engine;
  PyEngine::refsKeeper.update(this, _engine);

  move._ref = Py_NewRef(Py_None);
  return *this;
}

inline void GlobalRefState::swap(GlobalRefState& other){
  std::swap(_ref, other._ref);
  std::swap(_engine, other._engine);
  PyEngine::refsKeeper.update(this, _engine);
  PyEngine::refsKeeper.update(&other, other._engine);
}

inline bool GlobalRefState::isEmpty() const {
  return _ref == nullptr || Py_IsNone(_ref);
}

inline PyObject *GlobalRefState::get() const {
  return Py_NewRef(_ref);
}

inline PyObject *GlobalRefState::peek() const{
    return _ref;
}

inline void GlobalRefState::reset() {
  Py_XDECREF(_ref);
  _ref = Py_NewRef(Py_None);
}

inline void GlobalRefState::dtor(bool eraseFromList) {
  if(!_ref)
    return;     // is destroyed
  if(eraseFromList)
    PyEngine::refsKeeper.remove(this);
  Py_XDECREF(_ref);
  _ref = nullptr;
  _engine = nullptr;
}

}   // namespace py_backend

template <typename T>
Global<T>::Global() noexcept : val_() {}

template <typename T>
Global<T>::Global(const script::Local<T>& localReference) :val_(py_interop::peekPy(localReference)) {}

template <typename T>
Global<T>::Global(const script::Weak<T>& weak) : val_(weak.val_.peek()) {}

template <typename T>
Global<T>::Global(const script::Global<T>& copy) : val_(copy.val_) {}

template <typename T>
Global<T>::Global(script::Global<T>&& move) noexcept : val_(std::move(move.val_)) {}

template <typename T>
Global<T>::~Global() {
  val_.dtor();
}

template <typename T>
Global<T>& Global<T>::operator=(const script::Global<T>& assign) {
  val_ = assign.val_;
  return *this;
}

template <typename T>
Global<T>& Global<T>::operator=(script::Global<T>&& move) noexcept {
  val_ = std::move(move.val_);
  return *this;
}

template <typename T>
Global<T>& Global<T>::operator=(const script::Local<T>& assign) {
  auto state{py_backend::GlobalRefState(py_interop::peekPy(assign))};
  val_ = std::move(state);
  state.dtor();
  return *this;
}


template <typename T>
void Global<T>::swap(Global& rhs) noexcept {
  val_.swap(rhs.val_);
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
}

// =============== Weak ===============

namespace py_backend {

inline WeakRefState::WeakRefState() 
  :_ref(Py_NewRef(Py_None)), _engine(EngineScope::currentEngineAs<PyEngine>()) 
{
  PyEngine::refsKeeper.update(this, _engine);
}

inline WeakRefState::WeakRefState(PyObject* obj) 
  :_engine(EngineScope::currentEngineAs<PyEngine>()) 
{
  PyEngine::refsKeeper.update(this, _engine);
  if(Py_IsNone(obj))
  {
    _ref = Py_NewRef(Py_None);
    return;
  }

  _ref = PyWeakref_NewRef(obj, PyEngine::weakRefGcEmptyCallback);
  if(checkAndClearError() || !_ref)
  {
    // Fail to create weak ref, change to global ref
    _isRealWeakRef = false;
    _ref = Py_NewRef(obj);
  }
  else
    _isRealWeakRef = true;
}

inline WeakRefState::WeakRefState(const WeakRefState& assign) 
  :_engine(assign._engine) 
{
  PyEngine::refsKeeper.update(this, _engine);
  if(assign.isEmpty())
  {
    _ref = Py_NewRef(Py_None);
    return;
  }
  PyObject *originRef = assign.peek();
  if(assign._isRealWeakRef)
  {
    _ref = PyWeakref_NewRef(originRef, PyEngine::weakRefGcEmptyCallback);
    if(checkAndClearError() || !_ref)
    {
      // Fail to create weak ref, change to global ref
      _isRealWeakRef = false;
      _ref = Py_NewRef(originRef);
    }
    else
      _isRealWeakRef = true;
  }
  else
  {
    // assign is fake wake ref (global ref)
    _isRealWeakRef = false;
    _ref = Py_NewRef(originRef);
  }
}

inline WeakRefState::WeakRefState(WeakRefState&& move) noexcept
  :_engine(move._engine)
{
  PyEngine::refsKeeper.update(this, _engine);
  _isRealWeakRef = move._isRealWeakRef;
  _ref = move._ref;

  move._ref = Py_NewRef(Py_None);
  move._isRealWeakRef = false;
}

inline WeakRefState& WeakRefState::operator=(const WeakRefState& assign){
  Py_XDECREF(_ref);
  _engine = assign._engine;
  PyEngine::refsKeeper.update(this, _engine);

  if(assign.isEmpty())
  {
    _ref = Py_NewRef(Py_None);
    _isRealWeakRef = false;
    return *this;
  }

  PyObject *originRef = assign.peek();
  if(assign._isRealWeakRef)
  {
    _ref = PyWeakref_NewRef(originRef, PyEngine::weakRefGcEmptyCallback);
    if(checkAndClearError() || !_ref)
    {
      // Fail to create weak ref, change to global ref
      _isRealWeakRef = false;
      _ref = Py_NewRef(originRef);
    }
    else
      _isRealWeakRef = true;
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
  Py_XDECREF(_ref);

  _isRealWeakRef = move._isRealWeakRef;
  _ref = move._ref;
  _engine = move._engine;
  PyEngine::refsKeeper.update(this, _engine);

  move._ref = Py_NewRef(Py_None);
  move._isRealWeakRef = false;
  return *this;
}

inline void WeakRefState::swap(WeakRefState& other){
  std::swap(_isRealWeakRef, other._isRealWeakRef);
  std::swap(_ref, other._ref);
  std::swap(_engine, other._engine);
  PyEngine::refsKeeper.update(this, _engine);
  PyEngine::refsKeeper.update(&other, other._engine);
}

inline bool WeakRefState::isEmpty() const {
  PyObject *ref = peek();
  return ref == nullptr || Py_IsNone(ref);
}

inline PyObject *WeakRefState::get() const{
  if(_isRealWeakRef)
  {
    if(!PyWeakref_Check(_ref))
      return Py_NewRef(Py_None);      // error!
    PyObject* obj = PyWeakref_GetObject(_ref);
    return Py_NewRef(obj);
  }
  else
  {
    // is fake weak ref (global ref)
    return Py_NewRef(_ref);
  }
}

inline PyObject *WeakRefState::peek() const{
  if(_isRealWeakRef)
  {
    return (PyWeakref_Check(_ref) ? PyWeakref_GetObject(_ref) : Py_None);
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
  Py_XDECREF(_ref);
  _ref = Py_NewRef(Py_None);
  _isRealWeakRef = false;
}

inline void WeakRefState::dtor(bool eraseFromList) {
  if(!_ref)
    return;     // is destroyed
  if(eraseFromList)
    PyEngine::refsKeeper.remove(this);
  Py_XDECREF(_ref);
  _ref = nullptr;
  _isRealWeakRef = false;
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
Weak<T>::Weak(const script::Global<T>& globalReference) : val_(globalReference.val_.peek()) {}

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
Weak<T>& Weak<T>::operator=(const script::Local<T>& assign) {
  auto state{py_backend::WeakRefState(py_interop::peekPy(assign))};
  val_ = std::move(state);
  state.dtor();
  return *this;
}

template <typename T>
void Weak<T>::swap(Weak& rhs) noexcept {
  val_.swap(rhs.val_);
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
