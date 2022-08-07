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

// reference
// https://docs.python.org/3.8/c-api/init.html#thread-state-and-the-global-interpreter-lock
// https://stackoverflow.com/questions/26061298/python-multi-thread-multi-interpreter-c-api
// https://stackoverflow.com/questions/15470367/pyeval-initthreads-in-python-3-how-when-to-call-it-the-saga-continues-ad-naus

namespace script::py_backend {

PyEngineScopeImpl::PyEngineScopeImpl(PyEngine &engine, PyEngine *) {
  PyThreadState* currentThreadState = (PyThreadState*)engine.subThreadState.get();
  if(currentThreadState == NULL) {
    // create a new thread state for the the sub interpreter in the new thread
    currentThreadState = PyThreadState_New(engine.subInterpreterState);
    // save to TLS storage
    engine.subThreadState.set(currentThreadState);
  }

  // acquire the GIL & swap to correct thread state
  PyEval_RestoreThread(currentThreadState);
}
PyEngineScopeImpl::~PyEngineScopeImpl() {
  if(PyGILState_Check() > 0)
  {
    PyEval_SaveThread();        // release GIL & reset thread state
    //TODO: release unused thread state if needed
  }
}

PyExitEngineScopeImpl::PyExitEngineScopeImpl(PyEngine &) {
  if(PyGILState_Check() > 0)
  {
    PyEval_SaveThread();        // release GIL & reset thread state
  }
  //TODO: release unused thread state if needed
}


}  // namespace script::py_backend