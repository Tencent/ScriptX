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

#include "../../src/Native.hpp"
#include "../../src/utils/Helper.hpp"
#include "LuaEngine.h"
#include "LuaHelper.hpp"
#include "LuaReference.hpp"

namespace script::lua_backend {

/**
 *
 * The created binding class be like:
 *
 * \code{lua}
 *
 * Class = {}
 *
 * local staticMeta = {
 *  // constructor call
 *  __call = function()
 *      local ins = {};
 *      setmetatable(inx, instanceMeta);
 *      return ins
 *  end
 * }
 *
 * for staticFunc do
 *   Class[staticFunc.name] = function() ... end
 * end
 *
 * setmetatable(Class, staticMeta)
 *
 * local instanceMeta = {
 *   __index = function()
 *      1. find from `ClassDefine.instanceProperty`
 *      2. find from `ClassDefine.instanceFunction`
 *      3. return nil
 *   end
 *
 *   __newindex = function()
 *      1. find from `ClassDefine.instanceProperty`
 *      2. raw set to table
 *
 *   __gc = function()
 *      1. delete this
 *   end
 *
 *   instanceFunction = {...}
 *
 * }
 *
 * ScriptX.getInstanceMeta(Class) == instanceMeta;
 *
 * \endcode
 *
 * @tparam T
 * @param classDefine
 */
void LuaEngine::performRegisterNativeClass(
    internal::TypeIndex typeIndex, const internal::ClassDefineState* classDefine,
    script::ScriptClass* (*instanceTypeToScriptClass)(void*)) {
  StackFrameScope stackFrameScope;

  auto ns = ::script::internal::getNamespaceObject(this, classDefine->nameSpace,
                                                   get(kLuaGlobalEnvName).asObject())
                .asObject();

  lua_newtable(lua_);
  auto table = lua_gettop(lua_);
  lua_newtable(lua_);
  auto staticMeta = lua_gettop(lua_);

  registerStaticDefine(classDefine->staticDefine, table, staticMeta);

  if (classDefine->instanceDefine.constructor) {
    registerInstanceDefine(classDefine, table, staticMeta, instanceTypeToScriptClass);
  }

  lua_pushvalue(lua_, staticMeta);
  lua_setmetatable(lua_, table);

  auto obj = make<Local<Object>>(table);
  ns.set(classDefine->className, make<Local<Object>>(obj));

  nativeDefineRegistry_.emplace(classDefine, Global<Object>(make<Local<Object>>(obj)));
}

void LuaEngine::registerInstanceDefine(const internal::ClassDefineState* classDefine, int table,
                                       int staticMeta,
                                       script::ScriptClass* (*instanceTypeToScriptClass)(void*)) {
  StackFrameScope stack;

  lua_newtable(lua_);
  auto instanceMeta = lua_gettop(lua_);

  lua_newtable(lua_);
  auto instanceFunction = lua_gettop(lua_);

  defineInstanceProperties(classDefine, instanceMeta, instanceFunction);
  defineInstanceFunctions(classDefine, instanceFunction);
  defineInstanceConstructor(classDefine, instanceMeta, staticMeta, instanceTypeToScriptClass);

  make<Local<Object>>(instanceMeta)
      .set(kMetaTableBuiltInInstanceFunctions, make<Local<Object>>(instanceFunction));

  // built in data
  lua_pushvalue(lua_, instanceMeta);
  lua_rawsetp(lua_, table, kLuaBuiltinDefinedClassMetaDataToken_);

  lua_pushlightuserdata(lua_, const_cast<void*>(static_cast<const void*>(classDefine)));
  lua_rawsetp(lua_, table, kLuaTableNativeClassDefinePtrToken_);
}

void LuaEngine::defineInstanceConstructor(
    const internal::ClassDefineState* classDefine, int instanceMeta, int staticMeta,
    script::ScriptClass* (*instanceTypeToScriptClass)(void*)) const {
  luaEnsureStack(lua_, 4);

  {
    // key
    lua_pushstring(lua_, kLuaMetaMethodCall);

    // value
    lua_pushvalue(lua_, instanceMeta);
    lua_pushlightuserdata(lua_, const_cast<void*>(static_cast<const void*>(classDefine)));
    lua_pushlightuserdata(lua_, const_cast<LuaEngine*>(this));
    lua_pushlightuserdata(lua_, reinterpret_cast<void*>(instanceTypeToScriptClass));

    lua_pushcclosure(
        lua_,
        [](lua_State* lua) -> int {
          // lua __call: staticTable is it's first argument
          lua_remove(lua, 1);

          std::optional<std::string> exception;
          try {
            auto define =
                static_cast<internal::ClassDefineState*>(lua_touserdata(lua, lua_upvalueindex(2)));
            lua_newtable(lua);  // this table

            // copy instance functions
            //            luaCopyTable(lua, lua_upvalueindex(3), lua_gettop(lua));

            // set meta table
            lua_pushvalue(lua, lua_upvalueindex(1));
            lua_setmetatable(lua, -2);

            // dup
            lua_pushvalue(lua, -1);
            lua_rotate(lua, 1, 2);

            // stack state:
            // [this table]
            // [arg1]
            // [arg2]
            // [arg ...]

            void* thiz;
            auto argsBase = 2;
            auto argsCount = lua_gettop(lua) - 1;

            if (argsCount == 3 && lua_islightuserdata(lua, -2) &&
                lua_touserdata(lua, -2) == kLuaNativeConstructorMarker_) {
              // this logic is for
              // ScriptClass::ScriptClass(const ClassDefine<T> &define)
              thiz = static_cast<void*>(lua_touserdata(lua, -1));
            } else {
              // this logic is for
              // ScriptClass::ScriptClass(const Local<Object>& thiz)

              auto engine = static_cast<LuaEngine*>(lua_touserdata(lua, lua_upvalueindex(3)));
              thiz = define->instanceDefine.constructor(
                  makeArguments(engine, argsBase, argsCount, true));
            }
            auto instanceTypeToScriptClass = reinterpret_cast<script::ScriptClass* (*)(void*)>(
                lua_touserdata(lua, lua_upvalueindex(4)));
            ScriptClass* scriptClass = instanceTypeToScriptClass(thiz);

            lua_settop(lua, 1);
            // set ptr as this
            lua_pushlightuserdata(lua, thiz);
            lua_rawsetp(lua, 1, kLuaTableNativeThisPtrToken_);

            // set ScriptClass*
            lua_pushlightuserdata(lua, scriptClass);
            lua_rawsetp(lua, 1, kLuaTableNativeScriptClassPtrToken_);

            lua_pushlightuserdata(lua, define);
            lua_rawsetp(lua, 1, kLuaTableNativeClassDefinePtrToken_);
            return 1;
          } catch (const Exception& e) {
            exception = e.message();
          }

          luaThrow(lua, exception);
          return 0;
        },
        4);

    lua_rawset(lua_, staticMeta);
  }

  {
    lua_pushstring(lua_, kLuaMetaMethodNewGc);

    lua_pushcfunction(lua_, [](lua_State* lua) {
      lua_rawgetp(lua, 1, kLuaTableNativeScriptClassPtrToken_);
      ExitEngineScope exit;
      delete static_cast<ScriptClass*>(lua_touserdata(lua, -1));
      return 0;
    });
    lua_rawset(lua_, instanceMeta);
  }
}

void LuaEngine::defineInstanceFunctions(const internal::ClassDefineState* classDefine,
                                        int instanceFunctionTable) const {
  for (auto& funcDefine : classDefine->instanceDefine.functions) {
    {
      using FD = typename internal::InstanceDefine::FunctionDefine;
      lua_pushstring(lua_, funcDefine.name.c_str());
      pushInstanceFunction(
          &funcDefine, classDefine,
          [](lua_State* lua, void* data, void* thiz, const Arguments& args) -> Local<Value> {
            auto fd = static_cast<FD*>(data);
            Tracer trace(args.engine(), fd->traceName);
            return fd->callback(thiz, args);
          });
      lua_rawset(lua_, instanceFunctionTable);
    }
  }
}

void LuaEngine::defineInstanceProperties(const internal::ClassDefineState* classDefine,
                                         int instanceMeta, int instanceFunction) const {
  lua_newtable(lua_);
  auto getter = lua_gettop(lua_);
  lua_newtable(lua_);
  auto setter = lua_gettop(lua_);

  setupMetaTableForProperties(instanceMeta, instanceFunction, getter, setter);

  for (auto& propDef : classDefine->instanceDefine.properties) {
    using PD = typename internal::InstanceDefine::PropertyDefine;

    lua_pushstring(lua_, propDef.name.c_str());
    pushInstanceFunction(
        &propDef, classDefine,
        [](lua_State* /*lua*/, void* data, void* thiz, const Arguments& args) -> Local<Value> {
          // __index(table, index)
          auto pf = static_cast<PD*>(data);
          if (pf->getter) {
            Tracer trace(args.engine(), pf->traceName);
            return pf->getter(thiz);
          }
          return {};
        });
    lua_rawset(lua_, getter);

    lua_pushstring(lua_, propDef.name.c_str());
    pushInstanceFunction(
        &propDef, classDefine,
        [](lua_State* /*lua*/, void* data, void* thiz, const Arguments& args) -> Local<Value> {
          // __newindex(table, index, value)

          auto pf = static_cast<PD*>(data);
          if (pf->setter) {
            Tracer trace(args.engine(), pf->traceName);
            pf->setter(thiz, args[1]);
          }
          return {};
        });
    lua_rawset(lua_, setter);
  }
}

Local<Object> LuaEngine::performNewNativeClass(internal::TypeIndex typeIndex,
                                               const internal::ClassDefineState* classDefine,
                                               size_t size, const Local<script::Value>* args) {
  auto it = nativeDefineRegistry_.find(classDefine);
  if (it == nativeDefineRegistry_.end()) {
    throw Exception("class define[" + classDefine->className + "] is not registered");
  }

  return luaNewObject(it->second.getValue(), size, args);
}

bool LuaEngine::performIsInstanceOf(const Local<script::Value>& value,
                                    const internal::ClassDefineState* classDefine) {
  return isInstanceOf(lua_, classDefine, localRefIndex(value));
}

void* LuaEngine::performGetNativeInstance(const Local<script::Value>& value,
                                          const internal::ClassDefineState* classDefine) {
  return getNativeThis(lua_, classDefine, localRefIndex(value));
}

}  // namespace script::lua_backend
