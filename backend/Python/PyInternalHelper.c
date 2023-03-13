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



// Attention! This file is compiled as C code
// Because below two internal source header files cannot pass compile in CPP

#include <Python.h>
#include <pystate.h>
#define Py_BUILD_CORE       // trick here, as we must need some structures' members
#include <internal/pycore_interp.h>
#include <internal/pycore_runtime.h>
#undef Py_BUILD_CORE

// =========================================
// - Attention! Functions and definitions below is copied from CPython source code so they 
//  may need to be re-adapted as the CPython backend's version is updated.
// - These function and definitions are not exported. We can only copy the implementation.


// =========== From Source Code <pystate.c> ===========
#define HEAD_LOCK(runtime) \
    PyThread_acquire_lock((runtime)->interpreters.mutex, WAIT_LOCK)
#define HEAD_UNLOCK(runtime) \
    PyThread_release_lock((runtime)->interpreters.mutex)


// =========== From Source Code <pystate.c> ===========
/*
 * Delete all thread states except the one passed as argument.
 * Note that, if there is a current thread state, it *must* be the one
 * passed as argument.  Also, this won't touch any other interpreters
 * than the current one, since we don't know which thread state should
 * be kept in those other interpreters.
 */
void _PyThreadState_DeleteExcept(/*_PyRuntimeState *runtime,*/ PyThreadState *tstate)
{
    _PyRuntimeState *runtime = tstate->interp->runtime;
    PyInterpreterState *interp = tstate->interp;

    HEAD_LOCK(runtime);
    /* Remove all thread states, except tstate, from the linked list of
       thread states.  This will allow calling PyThreadState_Clear()
       without holding the lock. */
    PyThreadState *list = interp->tstate_head;
    if (list == tstate) {
        list = tstate->next;
    }
    if (tstate->prev) {
        tstate->prev->next = tstate->next;
    }
    if (tstate->next) {
        tstate->next->prev = tstate->prev;
    }
    tstate->prev = tstate->next = NULL;
    interp->tstate_head = tstate;
    HEAD_UNLOCK(runtime);

    /* Clear and deallocate all stale thread states.  Even if this
       executes Python code, we should be safe since it executes
       in the current thread, not one of the stale threads. */
    PyThreadState *p, *next;
    for (p = list; p; p = next) {
        next = p->next;
        PyThreadState_Clear(p);
        PyMem_RawFree(p);
    }
}

// =========================================

void SetPyInterpreterStateFinalizing(PyInterpreterState *is)
{
    is->finalizing = 1;
}