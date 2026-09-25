#pragma once

/**
 * @brief Logs how close each task has come to the end of its stack (F-33).
 *
 * A stack that is too small fails as corruption, not as an error, so the
 * sizes in THREADING_MODEL.md need measuring on the bench under load: driving
 * all four throttles, flicking between screens, a reconnect or two.
 *
 * Compiled out unless CONFIG_THROTTLE_STACK_REPORT is set (Diagnostics in
 * menuconfig); both calls are then empty.
 */
class StackReport {
public:
    StackReport() = delete;

    /** Starts a task that logs every known task's high-water mark every 10 s. */
    static void start();

    /** For one-shot tasks, which the periodic report rarely catches: logs the
     * calling task's high-water mark. Call just before it deletes itself. */
    static void logSelf();
};
