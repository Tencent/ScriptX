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

#include "../../src/types.h"

SCRIPTX_BEGIN_INCLUDE_LIBRARY
#include <v8.h>
SCRIPTX_END_INCLUDE_LIBRARY

// V8 public API changes
// https://v8.dev/docs/api
// https://docs.google.com/document/d/1g8JFi8T_oAE_7uAri7Njtig7fKaPDfotU6huOa1alds/edit
//
// node.js VS V8 version table
// https://nodejs.org/en/download/releases
//
// 1. to find line-of-code history (git blame)
//    git log --full-history -S 'V8_DEPRECATED("Use DisposePlatform()")' include/v8-initialization.h
// 2. to find tag version
//    git tag --contains 367074

// V8 version check helper
// V8_version >= version
#define SCRIPTX_V8_VERSION_GE(major, minor) \
  (V8_MAJOR_VERSION > (major) || (V8_MAJOR_VERSION == (major) && V8_MINOR_VERSION >= (minor)))

// V8_version <= version
#define SCRIPTX_V8_VERSION_LE(major, minor) \
  (V8_MAJOR_VERSION < (major) || (V8_MAJOR_VERSION == (major) && V8_MINOR_VERSION <= (minor)))

// old_version <= V8_version <= new_version
#define SCRIPTX_V8_VERSION_BETWEEN(old_major, old_minor, new_major, new_minor) \
  SCRIPTX_V8_VERSION_GE(old_major, old_minor) && SCRIPTX_V8_VERSION_LE(new_major, new_minor)

namespace script::v8_backend {

class V8Engine;

// forward declare to solve recursive include issue
// implemented in V8Engine.h
V8Engine* currentEngine();

V8Engine& currentEngineChecked();

v8::Isolate* currentEngineIsolateChecked();

v8::Local<v8::Context> currentEngineContextChecked();

std::tuple<v8::Isolate*, v8::Local<v8::Context>> currentEngineIsolateAndContextChecked();

void checkException(v8::TryCatch& tryCatch);

void rethrowException(const Exception& exception);

}  // namespace script::v8_backend

namespace script {
struct v8_interop;
}
