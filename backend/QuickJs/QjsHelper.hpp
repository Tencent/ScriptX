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
#include "../../src/Native.h"
#include "QjsHelper.h"
#include "QjsReference.hpp"

namespace script {

struct qjs_interop {
 private:
  qjs_interop() = default;

  template <typename T>
  Local<T> makeLocalInternal(JSValue value) {
    return Local<T>(value);
  }

  template <>
  Local<ByteBuffer> makeLocalInternal(JSValue value) {
    return Local<ByteBuffer>(qjs_backend::ByteBufferState(value));
  }

 public:
  /**
   * @tparam T
   * @param value owned(passing ownership)
   * @return
   */
  template <typename T>
  static Local<T> makeLocal(JSValue value) {
    // workaround that C++ don't support specialize static member function
    // https://stackoverflow.com/questions/29041775/explicit-template-specialization-cannot-have-a-storage-class-member-method-spe
    return qjs_interop().template makeLocalInternal<T>(value);
  }

  /**
   *
   * @tparam T
   * @param ref
   * @return not owned
   */
  template <typename T>
  static JSValue peekLocal(const Local<T>& ref) {
    return ref.val_;
  }

  /**
   *
   * @tparam T
   * @param ref
   * @return owned
   */
  template <typename T>
  static JSValue getLocal(const Local<T>& ref, JSContext* context = nullptr) {
    return qjs_backend::dupValue(ref.val_, context);
  }

  /**
   * @param engine
   * @param thiz not own
   * @param argc
   * @param argv not own
   * @return
   */
  static script::Arguments makeArguments(qjs_backend::QjsEngine* engine, JSValue thiz, size_t argc,
                                         JSValue* argv) {
    return script::Arguments(qjs_backend::ArgumentsData{engine, thiz, argc, argv});
  }
};

namespace qjs_backend {

Local<Function> newRawFunction(JSContext* context, void* data, RawFunctionCallback);

}  // namespace qjs_backend

}  // namespace script