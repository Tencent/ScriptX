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

#include "PyEngine.h"
#include <cstring>
#include "../../src/Utils.h"
#include "../../src/utils/Helper.hpp"

namespace script::py_backend {

PyEngine::PyEngine(std::shared_ptr<utils::MessageQueue> queue)
    : queue_(queue ? std::move(queue) : std::make_shared<utils::MessageQueue>()) {
  if (Py_IsInitialized() == 0) {
    Py_SetStandardStreamEncoding("utf-8", nullptr);
    // Python not initialized. Init main interpreter
    Py_Initialize();
    // Init threading environment
    PyEval_InitThreads();
    // Initialize type
    g_scriptx_property_type = makeStaticPropertyType();
    //  Save main thread state & release GIL
    mainThreadState_ = PyEval_SaveThread();
  }

  // Resume main thread state (to execute Py_NewInterpreter)
  PyThreadState* oldState = PyThreadState_Swap(mainThreadState_);

  // If GIL is released, lock it
  if (PyEngine::engineEnterCount == 0) {
    PyEval_AcquireLock();
  }
  // Create new interpreter
  PyThreadState* newSubState = Py_NewInterpreter();
  if (!newSubState) {
    throw Exception("Fail to create sub interpreter");
  }
  subInterpreterState_ = newSubState->interp;

  // If GIL is released before, unlock it
  if (PyEngine::engineEnterCount == 0) {
    PyEval_ReleaseLock();
  }
  // Store created new sub thread state & recover old thread state stored before
  subThreadState_.set(PyThreadState_Swap(oldState));
}

PyEngine::PyEngine() : PyEngine(nullptr) {}

PyEngine::~PyEngine() = default;

void PyEngine::destroy() noexcept {
  ScriptEngine::destroyUserData();  // TODO: solve this problem about Py_EndInterpreter
  /*if (PyEngine::engineEnterCount == 0) {
    // GIL is not locked. Just lock it
    PyEval_AcquireLock();
  }
  // Swap to clear thread state & end sub interpreter
  PyThreadState* oldThreadState = PyThreadState_Swap(subThreadState_.get());
  Py_EndInterpreter(subThreadState_.get());
  // Recover old thread state
  PyThreadState_Swap(oldThreadState);

  if (PyEngine::engineEnterCount == 0) {
      // Unlock the GIL because it is not locked before
      PyEval_ReleaseLock();
  }*/
}

Local<Value> PyEngine::get(const Local<String>& key) {
  PyObject* item = PyDict_GetItemString(getGlobalDict(), key.toStringHolder().c_str());
  if (item)
    return py_interop::toLocal<Value>(item);
  else
    return py_interop::toLocal<Value>(Py_None);
}

void PyEngine::set(const Local<String>& key, const Local<Value>& value) {
  int result =
      PyDict_SetItemString(getGlobalDict(), key.toStringHolder().c_str(), py_interop::getPy(value));
  if (result != 0) {
    checkException();
  }
}

Local<Value> PyEngine::eval(const Local<String>& script) { return eval(script, Local<Value>()); }

Local<Value> PyEngine::eval(const Local<String>& script, const Local<String>& sourceFile) {
  return eval(script, sourceFile.asValue());
}

Local<Value> PyEngine::eval(const Local<String>& script, const Local<Value>& sourceFile) {
  // Limitation: one line code must be expression (no "\n", no "=")
  const char* source = script.toStringHolder().c_str();
  bool oneLine = true;
  if (strstr(source, "\n") != NULL)
      oneLine = false;
  else if (strstr(source, " = ") != NULL)
      oneLine = false;
  PyObject* result = PyRun_StringFlags(source, oneLine ? Py_eval_input : Py_file_input,
                                       getGlobalDict(), nullptr, nullptr);
  if (result == nullptr) {
    checkException();
  }
  return py_interop::asLocal<Value>(result);
}

Local<Value> PyEngine::loadFile(const Local<String>& scriptFile) {
  std::string sourceFilePath = scriptFile.toString();
  if (sourceFilePath.empty()) {
    throw Exception("script file no found");
  }
  Local<Value> content = internal::readAllFileContent(scriptFile);
  if (content.isNull()) {
    throw Exception("can't load script file");
  }

  std::size_t pathSymbol = sourceFilePath.rfind("/");
  if (pathSymbol != -1) {
    sourceFilePath = sourceFilePath.substr(pathSymbol + 1);
  } else {
    pathSymbol = sourceFilePath.rfind("\\");
    if (pathSymbol != -1) sourceFilePath = sourceFilePath.substr(pathSymbol + 1);
  }
  Local<String> sourceFileName = String::newString(sourceFilePath);
  return eval(content.asString(), sourceFileName);
}

std::shared_ptr<utils::MessageQueue> PyEngine::messageQueue() { return queue_; }

void PyEngine::gc() {}

void PyEngine::adjustAssociatedMemory(int64_t count) {}

ScriptLanguage PyEngine::getLanguageType() { return ScriptLanguage::kPython; }

std::string PyEngine::getEngineVersion() { return Py_GetVersion(); }

bool PyEngine::isDestroying() const { return false; }
}  // namespace script::py_backend
