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
// One engine owns a sub-interpreter, and owns a TLS storage called engine.subThreadState_, 
// which stores his own thread state on each thread.
// 
// GIL keeps at one time only one engine can be running and this fucking situation is caused by 
// bad design of Python. Hope that GIL will be removed in next versions and sub-interpreter support
// will be public. Only that can save us from managing these annoying things manually
//

namespace script::py_backend {

PyEngineScopeImpl::PyEngineScopeImpl(PyEngine &engine, PyEngine *) {
  PyThreadState *currentThreadState = (PyThreadState *)engine.subThreadState_.get();
  if (currentThreadState == NULL) {
    // New thread entered first time with no threadstate
    // Create a new thread state for the the sub interpreter in the new thread
    currentThreadState = PyThreadState_New(engine.subInterpreterState_);
    // Save to TLS storage
    engine.subThreadState_.set(currentThreadState);
    // Save GIL held situation
    // See comments in ExitEngineScope
    engine.isGilHeldBefore = (bool)PyGILState_Check();

    std::cout << "========================= New thread state created." << std::endl;
    return;
  }
  else
  {
      // Thread state of this engine on current thread is inited & saved in TLS
      // Check if there is another existing thread state (is another engine entered)
      
      // PyThreadState_GET will cause FATEL error if oldState is null
      // so here get & check oldState by swap twice
      PyThreadState* oldState = PyThreadState_Swap(NULL);
      bool isOldStateNotEmpty = oldState != nullptr;
      PyThreadState_Swap(oldState);
      if (isOldStateNotEmpty) {
          // Another engine is entered
          // Push his thread state into stack & release GIL to avoid dead-lock
          engine.oldThreadStateStack_.push(PyEval_SaveThread());
          std::cout << "========================= Old thread state existing. Save to stack" << std::endl;
      }
      // acquire the GIL & swap to thread state of engine which is to enter
      PyEval_RestoreThread(currentThreadState);
      std::cout << "========================= Restore correct thread state." << std::endl;
  }
}

PyEngineScopeImpl::~PyEngineScopeImpl() {
  PyEngine *currentEngine = py_backend::currentEngine();
  if (currentEngine != nullptr) {
    // Engine existing. Need to exit
    PyExitEngineScopeImpl exitEngine(*currentEngine);
    std::cout << "========================= EngineScope destructor -> to exit" << std::endl;
  }
}

PyExitEngineScopeImpl::PyExitEngineScopeImpl(PyEngine &engine) {
  // If one thread is entered first and GIL is held
  // when we exit we need to avoid Release GIL to avoid that
  // return to the original thread with GIL not held & cause crash
  // So the situation of GIL is record before & process here

    // FIX HERE!!!! PROBLEM EXISTS

  if (engine.isGilHeldBefore)
  {
      // GIL is held before, so only clear thread state & don't release GIL
      PyThreadState_Swap(NULL);
      std::cout << "========================= Only clear current thread state" << std::endl;
      return;
  }
  else
  {
      // Release GIL & clear current thread state
      PyEval_SaveThread();  
      std::cout << "========================= Clear current thread state & release GIL" << std::endl;
  }
  
  // Restore old thread state saved & recover GIL if needed
  auto &oldThreadStateStack = engine.oldThreadStateStack_;
  if (!oldThreadStateStack.empty()) {
      std::cout << "========================= Restore old current thread state" << std::endl;
    PyEval_RestoreThread(oldThreadStateStack.top());
    oldThreadStateStack.pop();
  }
}

}  // namespace script::py_backend