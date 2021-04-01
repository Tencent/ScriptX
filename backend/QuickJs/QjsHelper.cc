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

// Copyright 2021 taylorcyang@tencent.com

/**
 * <pre>
 * Author: taylorcyang@tencent.com
 * Date:   2021-03-31
 * Time:   14:42
 * Life with Passion, Code with Creativity.
 * </pre>
 */
#include "QjsHelper.h"
#include <ScriptX/ScriptX.h>

namespace script::qjs_backend {

JSContext* currentContext() { return currentEngine().context_; }

JSRuntime* currentRuntime() { return currentEngine().runtime_; }

QjsEngine& currentEngine() { return EngineScope::currentEngineCheckedAs<qjs_backend::QjsEngine>(); }

void checkException(JSValue value) {
  if (JS_IsException(value)) {
    checkException(-1);
  }
}

void checkException(int ret, const char* message) {
  if (ret < 0) {
    auto context = currentContext();
    auto pending = JS_GetException(currentContext());

    if (JS_IsObject(pending)) {
      throw Exception(qjs_interop::makeLocal<Value>(pending));
    } else {
      JS_FreeValue(context, pending);
      throw Exception(std::string(message));
    }
  }
}

JSValue dupValue(JSValue val, JSContext* context) {
  return JS_DupValue(context ? context : currentContext(), val);
}

void freeValue(JSValue val, JSContext* context) {
  JS_FreeValue(context ? context : currentContext(), val);
}

JSValue throwException(const Exception& e, QjsEngine* engine) {
  JSContext* context = engine ? engine->context_ : currentContext();
  JS_Throw(context, qjs_interop::getLocal(e.exception()));
  return JS_UNDEFINED;
}

}  // namespace script::qjs_backend
