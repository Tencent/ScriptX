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

#include "../../src/Engine.h"
#include "../../src/Exception.h"
#include "../../src/utils/MessageQueue.h"
#include "PyHelper.hpp"

namespace script::py_backend {

class PyEngine : public ScriptEngine {
 private:
  std::shared_ptr<::script::utils::MessageQueue> queue_;

 public:
  PyEngine(std::shared_ptr<::script::utils::MessageQueue> queue);

  PyEngine();

  SCRIPTX_DISALLOW_COPY_AND_MOVE(PyEngine);

  void destroy() noexcept override;

  bool isDestroying() const override;

  Local<Value> get(const Local<String>& key) override;

  void set(const Local<String>& key, const Local<Value>& value) override;
  using ScriptEngine::set;

  Local<Value> eval(const Local<String>& script, const Local<Value>& sourceFile);
  Local<Value> eval(const Local<String>& script, const Local<String>& sourceFile) override;
  Local<Value> eval(const Local<String>& script) override;
  using ScriptEngine::eval;

  Local<Value> loadFile(const Local<String>& scriptFile) override;

  std::shared_ptr<utils::MessageQueue> messageQueue() override;

  void gc() override;

  void adjustAssociatedMemory(int64_t count) override;

  ScriptLanguage getLanguageType() override;

  std::string getEngineVersion() override;

 protected:
  ~PyEngine() override;

 private:
  template <typename T>
  bool registerNativeClassImpl(const ClassDefine<T>* classDefine) {
    try {
      if (classDefine == nullptr) {
        return false;
      }
      if constexpr (std::is_same_v<T, void>) {
        py::class_<void*> c(py::module_::import("builtins"), classDefine->getClassName().c_str());
        for (auto& method : classDefine->staticDefine.functions) {
          c.def_static(method.name.c_str(), [method](py::args args) {
            return py_interop::asPy(method.callback(
                py_interop::makeArguments(&py_backend::currentEngine(), py::dict(), args)));
          });
        }
        return c.check();
      } else {
        py::class_<T> c(py::module_::import("builtins"), classDefine->getClassName().c_str());
        if (classDefine->instanceDefine.constructor) {
          c.def(py::init([classDefine](py::args args) {
            T* instance = nullptr;
            instance = classDefine->instanceDefine.constructor(
                py_interop::makeArguments(&py_backend::currentEngine(), py::dict(), args));
            if (instance == nullptr) {
              throw Exception("can't create class " + classDefine->className);
            }
            return instance;
          }));
        }
        for (auto& method : classDefine->staticDefine.functions) {
          c.def(method.name.c_str(), [method](py::args args) {
            return py_interop::asPy(method.callback(
                py_interop::makeArguments(&py_backend::currentEngine(), py::dict(), args)));
          });
        }
        for (auto& method : classDefine->instanceDefine.functions) {
          c.def(method.name.c_str(), [method](T* instance, py::args args) {
            return py_interop::asPy(method.callback(
                instance,
                py_interop::makeArguments(&py_backend::currentEngine(), py::dict(), args)));
          });
        }
        for (auto& prop : classDefine->instanceDefine.properties) {
          if (prop.getter) {
            if (prop.setter) {
              c.def_property(
                  prop.name.c_str(),
                  [prop](T* instance) { return py_interop::asPy(prop.getter(instance)); },
                  [prop](T* instance, py::handle value) {
                    prop.setter(instance, Local<Value>(value));
                  });
            } else {
              c.def_property_readonly(prop.name.c_str(), [prop](T* instance) {
                return py_interop::asPy(prop.getter(instance));
              });
            }
          }
        }
        return c.check();
      }
    } catch (const py::builtin_exception& e) {
      throw Exception(e.what());
    } catch (const py::error_already_set& e) {
      throw Exception(e.what());
    }
  }

  Local<Object> getNamespaceForRegister(const std::string_view& nameSpace) {
    TEMPLATE_NOT_IMPLEMENTED();
  }

  template <typename T>
  Local<Object> newNativeClassImpl(const ClassDefine<T>* classDefine, size_t size,
                                   const Local<Value>* args) {
    try {
      // 返回T指针，接收const Argument& args
      py::tuple py_args(size);
      for (size_t i = 0; i < size; i++) {
        py_args[i] = py_interop::asPy(args[i]);
      }
      T* res = classDefine->instanceDefine.constructor(
          py_interop::makeArguments(&py_backend::currentEngine(), py::dict(), py_args));
      return Local<Object>(py::cast(res));
    } catch (const py::builtin_exception& e) {
      throw Exception(e.what());
    } catch (const py::error_already_set& e) {
      throw Exception(e.what());
    }
  }

  template <typename T>
  bool isInstanceOfImpl(const Local<Value>& value, const ClassDefine<T>* classDefine) {
    return py::isinstance<T>(value.val_);
  }

  template <typename T>
  T* getNativeInstanceImpl(const Local<Value>& value, const ClassDefine<T>* classDefine) {
    return value.val_.cast<T*>();
  }

 private:
  template <typename T>
  friend class ::script::Local;

  template <typename T>
  friend class ::script::Global;

  template <typename T>
  friend class ::script::Weak;

  friend class ::script::Object;

  friend class ::script::Array;

  friend class ::script::Function;

  friend class ::script::ByteBuffer;

  friend class ::script::ScriptEngine;

  friend class ::script::Exception;

  friend class ::script::Arguments;

  friend class ::script::ScriptClass;
};

}  // namespace script::py_backend