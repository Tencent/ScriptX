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

namespace script::py_backend {

PyEngine::PyEngine(std::shared_ptr<utils::MessageQueue> queue)
    : queue_(queue ? std::move(queue) : std::make_shared<utils::MessageQueue>()) {
  py::initialize_interpreter();
  py::register_exception<Exception>(py::module_::import("builtins"), "ScriptXException");
}

PyEngine::PyEngine() : PyEngine(nullptr) {}

PyEngine::~PyEngine() = default;

void PyEngine::destroy() noexcept { ScriptEngine::destroyUserData(); }

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

std::shared_ptr<utils::MessageQueue> PyEngine::messageQueue() { return queue_; }

void PyEngine::gc() {}

void PyEngine::adjustAssociatedMemory(int64_t count) {}

ScriptLanguage PyEngine::getLanguageType() { return ScriptLanguage::kPython; }

std::string PyEngine::getEngineVersion() { return Py_GetVersion(); }

bool PyEngine::isDestroying() const { return false; }

}  // namespace script::py_backend
