/*
 * Copyright (c) 2017-present Samsung Electronics Co., Ltd
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 */

#include "Escargot.h"
#include "Job.h"
#include "Context.h"
#include "VMInstance.h"
#include "SandBox.h"
#include "runtime/FinalizationRegistryObject.h"

namespace Escargot {

SandBox::SandBoxResult PromiseReactionJob::run()
{
    Context* context = relatedContext();
    ExecutionState state(context);

    if (UNLIKELY(context->vmInstance()->isPromiseHookRegistered())) {
        Object* promiseTarget = m_reaction.m_capability.m_promise;
        PromiseObject* promise = (promiseTarget && promiseTarget->isPromiseObject()) ? promiseTarget->asPromiseObject() : nullptr;
        context->vmInstance()->triggerPromiseHook(state, VMInstance::PromiseHookType::Before, promise, Value());
    }

#ifdef ESCARGOT_DEBUGGER
    Debugger* debugger = state.context()->debugger();
    ExecutionState* activeSavedStackTraceExecutionState = ESCARGOT_DEBUGGER_NO_STACK_TRACE_RESTORE;
    Debugger::SavedStackTraceDataVector* activeSavedStackTrace = nullptr;

    if (debugger != nullptr && m_reaction.m_capability.m_savedStackTrace != nullptr) {
        activeSavedStackTraceExecutionState = debugger->activeSavedStackTraceExecutionState();
        activeSavedStackTrace = debugger->activeSavedStackTrace();
        debugger->setActiveSavedStackTrace(&state, m_reaction.m_capability.m_savedStackTrace);
    }
#endif /* ESCARGOT_DEBUGGER */

    // https://www.ecma-international.org/ecma-262/10.0/#sec-promisereactionjob
    SandBox sandbox(context);
    SandBox::SandBoxResult result = sandbox.run([&]() -> Value {
        /* 25.4.2.1.4 Handler is "Identity" case */
        if (m_reaction.m_handler == (Object*)1) {
            Value value[] = { m_argument };
            return Object::call(state, m_reaction.m_capability.m_resolveFunction, Value(), 1, value);
        }

        /* 25.4.2.1.5 Handler is "Thrower" case */
        if (m_reaction.m_handler == (Object*)2) {
            Value value[] = { m_argument };
            return Object::call(state, m_reaction.m_capability.m_rejectFunction, Value(), 1, value);
        }

        SandBox sb(state.context());
        auto res = sb.run([&]() -> Value {
            Value arguments[] = { m_argument };
            Value res = Object::call(state, m_reaction.m_handler, Value(), 1, arguments);
            // m_reaction.m_capability can be null when there was no result capability when promise.then()
            if (m_reaction.m_capability.m_promise == nullptr) {
                return Value();
            }
            Value value[] = { res };
            return Object::call(state, m_reaction.m_capability.m_resolveFunction, Value(), 1, value);
        });
        if (!res.error.isEmpty()) {
            if (m_reaction.m_capability.m_rejectFunction) {
                Value reason[] = { res.error };
                return Object::call(state, m_reaction.m_capability.m_rejectFunction, Value(), 1, reason);
            } else {
                state.throwException(res.error);
            }
        }
        return res.result;
    });

#ifdef ESCARGOT_DEBUGGER
    if (activeSavedStackTraceExecutionState != ESCARGOT_DEBUGGER_NO_STACK_TRACE_RESTORE) {
        debugger->setActiveSavedStackTrace(activeSavedStackTraceExecutionState, activeSavedStackTrace);
    }
#endif /* ESCARGOT_DEBUGGER */

    if (UNLIKELY(context->vmInstance()->isPromiseHookRegistered())) {
        Object* promiseTarget = m_reaction.m_capability.m_promise;
        PromiseObject* promise = (promiseTarget && promiseTarget->isPromiseObject()) ? promiseTarget->asPromiseObject() : nullptr;
        context->vmInstance()->triggerPromiseHook(state, VMInstance::PromiseHookType::After, promise, Value());
    }

    return result;
}

SandBox::SandBoxResult PromiseResolveThenableJob::run()
{
    // https://www.ecma-international.org/ecma-262/10.0/#sec-promiseresolvethenablejob
    SandBox sandbox(relatedContext());
    ExecutionState state(relatedContext());
    return sandbox.run([&]() -> Value {
        auto strings = &state.context()->staticStrings();
        PromiseReaction::Capability capability = m_promise->createResolvingFunctions(state);

        SandBox sb(state.context());
        auto res = sb.run([&]() -> Value {
            Value arguments[] = { capability.m_resolveFunction, capability.m_rejectFunction };
            return Object::call(state, m_then, m_thenable, 2, arguments);
        });
        if (!res.error.isEmpty()) {
            Value reason[] = { res.error };
            return Object::call(state, capability.m_rejectFunction, Value(), 1, reason);
        }
        return res.result;
    });
}

SandBox::SandBoxResult CleanupSomeJob::run()
{
    auto oldCallback = m_object->m_cleanupCallback;
    m_object->m_cleanupCallback = m_callback;

    clearStack<1024>();
    GC_gcollect_and_unmap();
    GC_gcollect_and_unmap();
    GC_gcollect_and_unmap();
    m_object->m_cleanupCallback = oldCallback;

    SandBox::SandBoxResult result;
    result.result = Value();
    return result;
}
} // namespace Escargot
