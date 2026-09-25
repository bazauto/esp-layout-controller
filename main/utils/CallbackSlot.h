#pragma once

#include <functional>
#include <utility>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

template <typename Signature>
class CallbackSlot;

/**
 * @brief A callback set on one task and invoked from another (F-30).
 *
 * The slots on the clients and the controller are assigned on the LVGL or
 * main task and called from network and encoder tasks. A bare std::function
 * assigned while another task is inside operator() can tear or be destroyed
 * mid-call. This slot copies the function under its own mutex and invokes the
 * copy with the mutex released -- the pattern OrchestratorClient already used.
 *
 * The mutex is never held across the call itself. A callback that takes the
 * LVGL lock would otherwise order this mutex before LVGL's on one task while
 * the LVGL task, setting the slot, orders them the other way round.
 *
 * What copying does not give: clearing a slot does not wait for an invocation
 * already in flight. Whatever a callback reaches must outlive the slot being
 * cleared, which is why MainScreen is never destroyed (F-21).
 */
template <typename R, typename... Args>
class CallbackSlot<R(Args...)> {
public:
    using Function = std::function<R(Args...)>;

    CallbackSlot()
        : m_mutex(xSemaphoreCreateMutexStatic(&m_mutexBuffer))
    {
    }

    ~CallbackSlot() { vSemaphoreDelete(m_mutex); }

    CallbackSlot(const CallbackSlot&) = delete;
    CallbackSlot& operator=(const CallbackSlot&) = delete;

    void set(Function function)
    {
        // The old closure is destroyed outside the lock: its destructor is
        // arbitrary code.
        xSemaphoreTake(m_mutex, portMAX_DELAY);
        std::swap(m_function, function);
        xSemaphoreGive(m_mutex);
    }

    void clear() { set(Function()); }

    /** @return a copy, safe to call with no lock held. Empty when unset. */
    Function get() const
    {
        xSemaphoreTake(m_mutex, portMAX_DELAY);
        Function copy = m_function;
        xSemaphoreGive(m_mutex);
        return copy;
    }

    /** Invokes a copy if one is set. Returns nothing, so R is discarded. */
    template <typename... CallArgs>
    void operator()(CallArgs&&... args) const
    {
        Function function = get();
        if (function) {
            function(std::forward<CallArgs>(args)...);
        }
    }

private:
    StaticSemaphore_t m_mutexBuffer;
    SemaphoreHandle_t m_mutex;
    Function m_function;
};
