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
#include "../../src/Utils.h"
#include "../../src/utils/Helper.hpp"

namespace script::py_backend {

static PyObject* PyInit_scriptx() {
  auto m = py::module_("scriptx");
  // Register exception translation
  py::register_exception<Exception>(m, "ScriptXException");
  return m.ptr();
}

PyEngine::PyEngine(std::shared_ptr<utils::MessageQueue> queue)
    : queue_(queue ? std::move(queue) : std::make_shared<utils::MessageQueue>()) {
  if (Py_IsInitialized() == 0) {
    // Python not initialized. Init main interpreter
    Py_Initialize();
    // Enable thread support & get GIL
    PyEval_InitThreads();
    // Save main thread state & release GIL
    mainThreadState = PyEval_SaveThread();
  }

  PyThreadState* oldState = nullptr;
  if (py_backend::currentEngine() != nullptr) {
    // Another thread state exists, save it temporarily & release GIL
    // Need to save it here because Py_NewInterpreter need main thread state stored at
    // initialization
    oldState = PyEval_SaveThread();
  }

  // Acquire GIL & resume main thread state (to execute Py_NewInterpreter)
  PyEval_RestoreThread(mainThreadState);
  PyThreadState* newSubState = Py_NewInterpreter();
  if (!newSubState) throw Exception("Fail to create sub interpreter");
  subInterpreterState = newSubState->interp;

  // Add module to sub interpreter
  PyImport_AppendInittab("scriptx", PyInit_scriptx);
  // Store created new sub thread state & release GIL
  subThreadState.set(PyEval_SaveThread());

  // Recover old thread state stored before & recover GIL if needed
  if (oldState) {
    PyEval_RestoreThread(oldState);
  }
}

PyEngine::PyEngine() : PyEngine(nullptr) {}

PyEngine::~PyEngine() = default;

void PyEngine::destroy() noexcept {
  PyEval_AcquireThread((PyThreadState*)subThreadState.get());
  Py_EndInterpreter((PyThreadState*)subThreadState.get());
  ScriptEngine::destroyUserData();
}

Local<Value> PyEngine::get(const Local<String>& key) {
  PyObject* globals = getGlobalDict();
  if (globals == nullptr) {
    throw Exception("Fail to get globals");
  }
  PyObject* value = PyDict_GetItemString(globals, key.toStringHolder().c_str());
  return Local<Value>(value);
}

void PyEngine::set(const Local<String>& key, const Local<Value>& value) {
  PyObject* globals = getGlobalDict();
  if (globals == nullptr) {
    throw Exception("Fail to get globals");
  }
  int result =
      PyDict_SetItemString(globals, key.toStringHolder().c_str(), py_interop::getLocal(value));
  if (result != 0) {
    checkException();
  }
}

Local<Value> PyEngine::eval(const Local<String>& script) { return eval(script, Local<Value>()); }

Local<Value> PyEngine::eval(const Local<String>& script, const Local<String>& sourceFile) {
  return eval(script, sourceFile.asValue());
}

Local<Value> PyEngine::eval(const Local<String>& script, const Local<Value>& sourceFile) {
  try {
    std::string source = script.toString();
    if (source.find('\n') != std::string::npos)
      return Local<Value>(py::eval<py::eval_statements>(source));
    else
      return Local<Value>(py::eval<py::eval_single_statement>(source));
  } catch (const py::builtin_exception& e) {
    throw Exception(e.what());
  } catch (const py::error_already_set& e) {
    // Because of pybind11's e.what() use his own gil lock,
    // we need to let pybind11 know that we have created thread state and he only need to use it,
    // or he will twice-acquire GIL & cause dead-lock.
    // Code below is just adaptation for pybind11's gil acquire in his internal code
    auto& internals = py::detail::get_internals();
    PyThreadState* tempState = (PyThreadState*)PYBIND11_TLS_GET_VALUE(internals.tstate);
    PYBIND11_TLS_REPLACE_VALUE(internals.tstate, subThreadState.get());
    const char* errorStr = e.what();
    // PYBIND11_TLS_REPLACE_VALUE(internals.tstate, tempState);
    throw Exception(errorStr);
  }
  PyObject* result = nullptr;
  PyObject* globals = py_backend::getGlobalDict();
  if (oneLine) {
    result = PyRun_StringFlags(source, Py_single_input, globals, nullptr, nullptr);
  } else {
    result = PyRun_StringFlags(source, Py_file_input, globals, nullptr, nullptr);
  }
  if (result == nullptr) {
    checkException();
  }
  return Local<Value>(result);
}

Local<Value> PyEngine::loadFile(const Local<String>& scriptFile) {
  std::string sourceFilePath = scriptFile.toString();
  if (sourceFilePath.empty()) throw Exception("script file no found");
  Local<Value> content = internal::readAllFileContent(scriptFile);
  if (content.isNull()) throw Exception("can't load script file");

  std::size_t pathSymbol = sourceFilePath.rfind("/");
  if (pathSymbol != -1)
    sourceFilePath = sourceFilePath.substr(pathSymbol + 1);
  else {
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
