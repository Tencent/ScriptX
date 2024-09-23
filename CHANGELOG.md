Version 3.6.0 (2024-09):
1. `[V8]` add test infrastructure to test on multiple v8 versions
2. `[V8]` add support to v8 versions through 7.4~13.0

Version 3.5.0 (2023-05):
1. `[QuickJs]` add support for QuickJs 2024-01-13

Version 3.4.0 (2023-05):
1. `[V8]` **BEHAVIOR CHANGE:** `V8Platform` now being a singleton, not ref-counted by `V8Engine`s any more.
2. `[V8]` add support for V8 version 11.4
3. `[V8]` add test support for V8 arm64

---
Version 3.3.1 (2023-03):
1. `[V8]` add support for V8 version 10.8
2. `[V8]` add support for node.js version 19.8.1

---
Version 3.3.0 (2023-03):
Optimized template to reduce code bloat

size change of UnitTest for release build

Lua: 3097576 - 2758032 = 339,544
JavaScriptCore: 3670032 - 3380048 = 289,984
V8: 22610696 - 22240400 = 370,296
WebAssembly: 1640551 - 1444942 = 195,60

---
Version 3.2.0 (2021-12):
1. fix QuickJs memory leak, engine instance not deleted on destroy
2. fix QuickJs missing EngineScope when run micro task (Promise)

Version 3.1.0 (2021-04):
1. add QuickJs backend

---

Version 3.0.0 (2021-03):
1. Rename project name to ScriptX
2. prepare for open source

notice: user should
1. change `#include <ScriptEngine/ScriptEngine.h>` to `#include <ScriptX/ScirptX.h>`
2. change cmake config value to be SCRIPTX_xxx
3. don't need to change any code reference

---
Version 2.2.3 (2020-12):
1. workaround ios-9 issue that don't have ByteBuffer related api
2. Exception lazy creation script object
    1. improve exception throw performance
    2. only create script exception object on calling Exception::exception()
    3. avoid recursive call on exception constructor

---

Version 2.2.2 (2020-12):
1. fix bug MessageQueue::loopQueue(MessageQueue::LoopType::kLoopOnce) only execute one message
2. above fix also fix memory leaks on JavaScriptCore backends.

---
Version 2.2.1 (2020-11):
1. accommodate V8Platform.h for v8 version 8.6 and node.js 15.x
2. support write node.js add on using ScriptEngine, also added docs and demo project test/node_addon

---
Version 2.1.1 (2020-11):
1. Fix Local<ByteBuffer>::size semantic ambiguity, remove this method.
Added Local<ByteBuffer>::byteLength() and Local<ByteBuffer>::elementCount().

2. added ScriptEngine::setData/getData API to store user defined engine-related data.

3. use WarningAsError only for build UnitTest.

---
Version 2.0 (2020-10):
Add WebAssembly support.

---
Version 1.0 (2020-8):
API skeleton
V8 backend support.
JavaScriptCore backend support.
Lua backend support.
A Template backend which has no implementation.

---
Version 0.0 (2019-10):
Project started.
