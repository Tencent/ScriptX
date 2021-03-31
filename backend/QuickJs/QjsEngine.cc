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

#include "QjsEngine.h"
#include "../../src/Utils.h"

namespace script::qjs_backend {

QjsEngine::QjsEngine(std::shared_ptr<utils::MessageQueue> queue) {}

QjsEngine::QjsEngine() : QjsEngine(std::shared_ptr<utils::MessageQueue>{}) {}

QjsEngine::~QjsEngine() = default;

void QjsEngine::destroy() noexcept {}

Local<Value> QjsEngine::get(const Local<String>& key) { return Local<Value>(); }

void QjsEngine::set(const Local<String>& key, const Local<Value>& value) {}

Local<Value> QjsEngine::eval(const Local<String>& script) { return eval(script, Local<Value>()); }

Local<Value> QjsEngine::eval(const Local<String>& script, const Local<String>& sourceFile) {
  return eval(script, sourceFile.asValue());
}

Local<Value> QjsEngine::eval(const Local<String>& script, const Local<Value>& sourceFile) {
  return Local<Value>();
}

std::shared_ptr<utils::MessageQueue> QjsEngine::messageQueue() {
  return std::shared_ptr<utils::MessageQueue>();
}

void QjsEngine::gc() {}

void QjsEngine::adjustAssociatedMemory(int64_t count) {}

ScriptLanguage QjsEngine::getLanguageType() { return ScriptLanguage::kJavaScript; }

std::string QjsEngine::getEngineVersion() { return ""; }

bool QjsEngine::isDestroying() const { return false; }

}  // namespace script::qjs_backend
