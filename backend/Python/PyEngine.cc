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

PyEngine::PyEngine(std::shared_ptr<utils::MessageQueue> queue)
    : queue_(queue ? std::move(queue) : std::make_shared<utils::MessageQueue>()) {
  if (Py_IsInitialized() == 0) {
    py::initialize_interpreter();
    // enable thread support & get GIL
    PyEval_InitThreads();
    // register exception translation
    py::register_exception<Exception>(py::module_::import("builtins"), "ScriptXException");
    // save thread state & release GIL
    mainThreadState = PyEval_SaveThread();
  }

  PyEval_AcquireLock();     // acquire GIL
  PyThreadState* state = Py_NewInterpreter();
  subThreadState.set(state);
  subInterpreterState = state->interp;
  PyEval_ReleaseThread(state);    // release GIL
}

PyEngine::PyEngine() : PyEngine(nullptr) {}

PyEngine::~PyEngine() = default;

void PyEngine::destroy() noexcept {
  Py_EndInterpreter(defaultSubThreadState);
  ScriptEngine::destroyUserData();
}

Local<Value> PyEngine::get(const Local<String>& key) {
  return Local<Value>(py::globals()[key.toString().c_str()]);
}

void PyEngine::set(const Local<String>& key, const Local<Value>& value) {
  py::globals()[key.toString().c_str()] = value.val_;
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
    throw Exception(e.what());
  }
}

Local<Value> PyEngine::loadFile(const Local<String>& scriptFile) {
  if (scriptFile.toString().empty()) throw Exception("script file no found");
  Local<Value> content = internal::readAllFileContent(scriptFile);
  if (content.isNull()) throw Exception("can't load script file");

  std::string sourceFilePath = scriptFile.toString();
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
