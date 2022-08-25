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

#include "PyScope.h"
#include "PyEngine.h"
#include <iostream>

// Reference
// https://docs.python.org/3.8/c-api/init.html#thread-state-and-the-global-interpreter-lock
// https://stackoverflow.com/questions/26061298/python-multi-thread-multi-interpreter-c-api
// https://stackoverflow.com/questions/15470367/pyeval-initthreads-in-python-3-how-when-to-call-it-the-saga-continues-ad-naus
//
// Because python's bad support of sub-interpreter, here to manage GIL & thread state manually.
// - One engine owns a sub-interpreter, and owns a TLS storage called engine.subThreadState_, 
// which stores his own current thread state on each thread.
// - This "thread state" works like "CPU Context" in low-level C programs. When changing engine, 
// "context" need to be changed to his correct thread state
// - When entering a new EngineScope, first check that if an thread state exists. If found,
// save it into oldThreadStateStack. When exit this EngineScope, old thread state saved before
// will be poped and recovered.
// - GIL is locked when any EngineScope is entered, and it is a global state (which means that 
// this lock is shared by all threads). When the last EngineScope exited, the GIL will be released.
// 
// GIL keeps at one time only one engine can be running and this fucking situation is caused by 
// bad design of CPython. Hope that GIL will be removed in next versions and sub-interpreter support
// will be public. Only that can save us from managing these annoying things manually
//

namespace script::py_backend {

PyEngineScopeImpl::PyEngineScopeImpl(PyEngine &engine, PyEngine *) {
  // Get thread state to enter
  PyThreadState *currentThreadState = engine.subThreadState_.get();
  if (currentThreadState == NULL) {
    // New thread entered first time with no threadstate
    // Create a new thread state for the the sub interpreter in the new thread
    // correct thread state after this
    currentThreadState = PyThreadState_New(engine.subInterpreterState_);
    // Save to TLS storage
    engine.subThreadState_.set(currentThreadState);
  }
  else
  {
      // Thread state of this engine on current thread is inited & saved in TLS
      // Check if there is another existing thread state (is another engine entered)
      
      // PyThreadState_GET will cause FATEL error if oldState is NULL
      // so here get & check oldState by swap twice
      PyThreadState* oldState = PyThreadState_Swap(NULL);
      bool isOldStateNotEmpty = oldState != nullptr;
      PyThreadState_Swap(oldState);
      if (isOldStateNotEmpty) {
          // Another engine is entered
          // Push his thread state into stack 
          engine.oldThreadStateStack_.push(PyThreadState_Swap(NULL));
      }
      // Swap to thread state of engine which is to enter
      PyThreadState_Swap(currentThreadState);
  }

  // First enginescope to enter, so lock GIL
  if (PyEngine::engineEnterCount == 0)
  {
      PyEval_AcquireLock();
  }
  ++PyEngine::engineEnterCount;
  // GIL locked & correct thread state here
  // GIL will keep locked until last EngineScope exit
}

PyEngineScopeImpl::~PyEngineScopeImpl() {
  PyEngine *currentEngine = py_backend::currentEngine();
  if (currentEngine != nullptr) {
    // Engine existing. Need to exit
    PyExitEngineScopeImpl exitEngine(*currentEngine);
  }
}

PyExitEngineScopeImpl::PyExitEngineScopeImpl(PyEngine &engine) {
  if ((--PyEngine::engineEnterCount) == 0)
  {
      // This is the last enginescope to exit, so release GIL
      PyEval_ReleaseLock();
  }
  // Swap to clear thread state
  PyThreadState_Swap(NULL);
  
  // Restore old thread state saved if needed
  auto oldThreadStateStack = engine.oldThreadStateStack_;
  if (!oldThreadStateStack.empty()) {
    PyThreadState_Swap(oldThreadStateStack.top());
    oldThreadStateStack.pop();
  }
}

}  // namespace script::py_backend