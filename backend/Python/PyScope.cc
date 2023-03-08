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
// Because python's bad support of sub-interpreter, we need to manage GIL & thread state manually.
//
// - One engine owns a sub-interpreter, and owns a TLS storage called engine.subThreadState_, 
//   which stores his own current thread state on each thread.
// - This "thread state" works like "CPU Context". When changing engine, "context" need to be
//   switched to correct target thread state.
//
// - One sub-interpreter may own more than one thread states. Each thread state corresponds to 
//   one thread.
// - When a sub-interpreter is created, a thread state for current thread will be created too. 
// - In default, this sub-interpreter can only be used in the thread which he was created. 
//   When we need to use this sub-interpreter in a new thread, we need to create thread state
//   for it manually in that new thread before using it.
//
// - Implementations: 
//   1. When entering a new EngineScope, first check that if there is another existing thread 
//     state loaded now (For example, put by another engine before). If exists, push the old 
//     one into oldThreadStateStack.
//   2. Then check that if an thread state stored in engine's TLS storage subThreadState_.
//   - If found a stored thread state, just load it.
//   - If the TLS storage is empty, it means that this engine enters this thread for the first 
//     time. So create a new thread state for it manually (and load it too), then save it 
//     to TLS storage subThreadState_.
//   3. When exiting an EngineScope, if old thread state is saved before, it will be poped and 
//     recovered.
//   4. GIL is locked when any EngineScope is entered, and it is a global state (which means that 
//     this lock is shared by all threads). When the last EngineScope exited, the GIL will be released.
// 
// GIL keeps at one time only one thread can be running. This unpleasant situation is caused by 
// bad design of CPython. Hope that GIL will be removed in next versions and sub-interpreter support
// will be public. Only that can save us from managing these annoying things manually
//

namespace script::py_backend {

EngineScopeImpl::EngineScopeImpl(PyEngine &engine, PyEngine * enginePtr) {
  // Check if there is another existing thread state (put by another engine)   
  // PyThreadState_GET will cause FATAL error if oldState is NULL
  // so here get & check oldState by swap twice
  PyThreadState* oldState = PyThreadState_Swap(NULL);
  bool isOldStateNotEmpty = oldState != nullptr;
  PyThreadState_Swap(oldState);
  if (isOldStateNotEmpty) {
      // Another thread state is loaded
      // Push the old one into stack 
      PyEngine::oldThreadStateStack_.push({PyThreadState_Swap(NULL), false});
  }
  else
  {
    // Push a NULL into stack, means that no need to recover when exit EngineScope
    PyEngine::oldThreadStateStack_.push({NULL, false});
  }

  // Get current engine's thread state in TLS storage
  PyThreadState *currentThreadState = engine.subThreadStateInTLS_.get();
  if (currentThreadState == NULL) {
    // Sub-interpreter enter new thread first time with no thread state
    // Create a new thread state for the the sub interpreter in the new thread
    currentThreadState = PyThreadState_New(engine.subInterpreterState_);
    // Save to TLS storage
    engine.subThreadStateInTLS_.set(currentThreadState);

    // Load the thread state created just now
    PyThreadState_Swap(currentThreadState);
  }
  else
  {
    // Thread state of this engine on current thread is inited & saved in TLS
    // Just load it
    PyThreadState_Swap(currentThreadState);
  }

  if (PyEngine::engineEnterCount_ == 0)
  {
    // This is first EngineScope to enter, so lock GIL
    PyEval_AcquireLock();
  }
  ++PyEngine::engineEnterCount_;
  // GIL locked & correct thread state here
  // GIL will keep locked until last EngineScope exit
}

EngineScopeImpl::~EngineScopeImpl() {
  auto &oldStatesStack = PyEngine::oldThreadStateStack_;
  if(oldStatesStack.empty())
  {
    // why? it cannot be empty here!
    throw Exception("Bad old_thread_state_stack status");
  }
  auto &topData = oldStatesStack.top();
  if(topData.threadState == PyEngine::mainThreadState_)
  {
    // why? it cannot be main thread state here!
    throw Exception("Bad old_thread_state_stack status");
  }
  if(!topData.aboveScopeIsExited)
  {
    // Current scope has not been exited. Exit it
    PyEngine *currentEngine = py_backend::currentEngine();
    ExitEngineScopeImpl exit(*currentEngine);
  }
  // Set old thread state stored back
  PyThreadState_Swap(topData.threadState);
  oldStatesStack.pop();
}

ExitEngineScopeImpl::ExitEngineScopeImpl(PyEngine &engine) {
  if(PyEngine::oldThreadStateStack_.empty())
  {
    // why? it cannot be empty here!
    throw Exception("Cannot exit an EngineScope when no EngineScope is entered");
  }
  auto &topData = PyEngine::oldThreadStateStack_.top();
  if(topData.threadState == PyEngine::mainThreadState_)
  {
    // why? it cannot be main thread state here!
    throw Exception("Cannot exit an EngineScope when no EngineScope is entered");
  }
  if(topData.aboveScopeIsExited)
  {
    // Current scope has been exited. Nothing need to do here
    return;
  }
  else
  {
    // Exit current scope
    topData.aboveScopeIsExited = true;

    if ((--PyEngine::engineEnterCount_) == 0)
    {
        // This is the last enginescope to exit, so release GIL
        PyEval_ReleaseLock();
    }
    // Swap to clear thread state
    PyThreadState_Swap(NULL);

    // Do not pop topData here. Let the dtor of EngineScope to do pop and recover work later.
  }
}

}  // namespace script::py_backend