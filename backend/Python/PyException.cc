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
#include "PyHelper.h"

namespace script {

namespace py_backend {

void ExceptionFields::fillMessage() const noexcept {
  if (exception_.isEmpty() || exception_.getValue().isString()) {
    return;
  }
  PyObject *capsule = py_interop::peekPy(exception_.getValue());
  if (!PyCapsule_IsValid(capsule, nullptr)) {
    return;
  }
  ExceptionInfo *errStruct =
      (ExceptionInfo *)PyCapsule_GetPointer(capsule, nullptr);

  PyTypeObject *typeObj = (PyTypeObject *)(errStruct->pType);
  PyObject *formattedMsg = PyObject_Str(errStruct->pValue);
  if (!formattedMsg) {
    return;
  }
  // NameError: name 'hello' is not defined
  message_ = std::string(typeObj->tp_name) + ": " + PyUnicode_AsUTF8(formattedMsg);
  hasMessage_ = true;
}

void ExceptionFields::fillStacktrace() const noexcept {
  if (exception_.isEmpty() || exception_.getValue().isString()) {
    return;
  }
  PyObject *capsule = py_interop::peekPy(exception_.getValue());
  if (!PyCapsule_IsValid(capsule, nullptr)) {
    return;
  }
  ExceptionInfo *errStruct =
      (ExceptionInfo *)PyCapsule_GetPointer(capsule, nullptr);

  PyTracebackObject *tb = (PyTracebackObject *)(errStruct->pTraceback);
  if (tb == nullptr)
      return;

  // Get the deepest trace possible.
  while (tb->tb_next) {
    tb = tb->tb_next;
  }
  PyFrameObject *frame = tb->tb_frame;
  Py_XINCREF(frame);
  stacktrace_ = "Traceback (most recent call last):";
  while (frame) {
    stacktrace_ += '\n';
    PyCodeObject *f_code = PyFrame_GetCode(frame);
    int lineno = PyFrame_GetLineNumber(frame);
    stacktrace_ += "  File \"";
    stacktrace_ += PyUnicode_AsUTF8(f_code->co_filename);
    stacktrace_ += "\", line ";
    stacktrace_ += std::to_string(lineno);
    stacktrace_ += ", in ";
    stacktrace_ += PyUnicode_AsUTF8(f_code->co_name);
    Py_DECREF(f_code);
    frame = frame->f_back;
  }
  hasStacktrace_ = true;
}

}  // namespace py_backend

Exception::Exception(std::string msg) : std::exception(), exception_() {
  exception_.message_ = msg;
  exception_.hasMessage_ = true;
}

Exception::Exception(const script::Local<script::String> &message)
    : std::exception(), exception_() {
  exception_.exception_ = message;
  exception_.hasMessage_ = true;
}

Exception::Exception(const script::Local<script::Value> &exception)
    : std::exception(), exception_({}) {
  exception_.exception_ = exception;
}

Local<Value> Exception::exception() const {
  if (exception_.exception_.isEmpty()) {
    exception_.exception_ = String::newString(exception_.message_);
  }
  return exception_.exception_.getValue();
}

std::string Exception::message() const noexcept {
  exception_.fillMessage();
  return exception_.hasMessage_ ? exception_.message_ : "[No Exception Message]";
}

std::string Exception::stacktrace() const noexcept {
  exception_.fillStacktrace();
  return exception_.hasStacktrace_ ? exception_.stacktrace_ : "[No Stacktrace]";
}

const char *Exception::what() const noexcept {
  exception_.fillMessage();
  return exception_.hasMessage_ ? exception_.message_.c_str() : "[No Exception Message]";
}

}  // namespace script
