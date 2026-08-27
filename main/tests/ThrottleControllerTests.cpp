#include "unity.h"
#include "ThrottleController.h"
#include "ThrottleBackend.h"
#include "Locomotive.h"

namespace {
    struct UiCallbackState {
        int calls = 0;
    };

    /**
     * @brief Recording ThrottleBackend for controller tests.
     *
     * These tests used to construct a real WiThrottleClient purely as
     * something to point the controller at -- it never connected, so every
     * command went nowhere unobserved. A fake records instead, which is what
     * lets the tests below assert that a knob turn actually reaches the
     * transport, and keeps the unit tests off anything that opens a socket.
     */
    class FakeThrottleBackend : public ThrottleBackend {
    public:
        struct Call {
            int throttleId = -1;
            int intArg = 0;
            bool boolArg = false;
        };

        // Defaults match WiThrottle, so the existing tests keep the behaviour
        // they were written against. Individual tests override them.
        bool acquisitionRequired = true;
        bool rosterProvided = true;
        bool labelsProvided = true;
        bool pollingRequired = true;
        bool connected = true;
        std::vector<RosterEntry> roster;

        std::vector<Call> acquires;
        std::vector<Call> releases;
        std::vector<Call> speeds;
        std::vector<Call> directions;
        std::vector<Call> functions;
        std::vector<Call> refreshes;

        bool requiresAcquisition() const override { return acquisitionRequired; }
        bool providesRoster() const override { return rosterProvided; }
        bool providesFunctionLabels() const override { return labelsProvided; }
        bool requiresPolling() const override { return pollingRequired; }

        bool isConnected() const override { return connected; }
        ConnectionState getState() const override {
            return connected ? ConnectionState::CONNECTED : ConnectionState::DISCONNECTED;
        }

        esp_err_t acquireLocomotive(int throttleId, int address, bool longAddress) override {
            acquires.push_back({throttleId, address, longAddress});
            return ESP_OK;
        }
        esp_err_t releaseLocomotive(int throttleId) override {
            releases.push_back({throttleId, 0, false});
            return ESP_OK;
        }
        esp_err_t setSpeed(int throttleId, int speed) override {
            speeds.push_back({throttleId, speed, false});
            return ESP_OK;
        }
        esp_err_t setDirection(int throttleId, bool forward) override {
            directions.push_back({throttleId, 0, forward});
            return ESP_OK;
        }
        esp_err_t setFunction(int throttleId, int function, bool state) override {
            functions.push_back({throttleId, function, state});
            return ESP_OK;
        }
        esp_err_t refreshThrottleState(int throttleId) override {
            refreshes.push_back({throttleId, 0, false});
            return ESP_OK;
        }

        size_t getRosterSize() const override { return roster.size(); }
        bool getRosterEntry(int index, RosterEntry& outEntry) const override {
            if (index < 0 || static_cast<size_t>(index) >= roster.size()) {
                return false;
            }
            outEntry = roster[index];
            return true;
        }

        void setThrottleStateCallback(ThrottleStateCallback callback) override {
            throttleStateCallback = std::move(callback);
        }
        void setFunctionLabelsCallback(FunctionLabelsCallback callback) override {
            functionLabelsCallback = std::move(callback);
        }

        /** Drives the controller from the transport side, as a real backend
         * would when the server reports a change we did not make. */
        void emitThrottleUpdate(const ThrottleUpdate& update) {
            if (throttleStateCallback) {
                throttleStateCallback(update);
            }
        }

        ThrottleStateCallback throttleStateCallback;
        FunctionLabelsCallback functionLabelsCallback;
    };

    void uiUpdateCallback(void* userData)
    {
        auto* state = static_cast<UiCallbackState*>(userData);
        if (state) {
            state->calls++;
        }
    }

    void setupThrottleWithLoco(ThrottleController& controller, int throttleId, int knobId, const char* name, int address)
    {
        Throttle* throttle = controller.getThrottle(throttleId);
        Knob* knob = controller.getKnob(knobId);
        TEST_ASSERT_NOT_NULL(throttle);
        TEST_ASSERT_NOT_NULL(knob);

        // Force into selecting state and assign loco
        TEST_ASSERT_TRUE(throttle->assignKnob(knobId));
        knob->assignToThrottle(throttleId);

        auto loco = std::make_unique<Locomotive>(name, address, Locomotive::AddressType::SHORT);
        TEST_ASSERT_TRUE(throttle->assignLocomotive(std::move(loco)));
        knob->startControlling();
    }

    void setupThrottleAllocatedNoKnob(ThrottleController& controller, int throttleId, int knobId, const char* name, int address)
    {
        setupThrottleWithLoco(controller, throttleId, knobId, name, address);

        Throttle* throttle = controller.getThrottle(throttleId);
        Knob* knob = controller.getKnob(knobId);
        TEST_ASSERT_NOT_NULL(throttle);
        TEST_ASSERT_NOT_NULL(knob);

        throttle->unassignKnob();
        knob->release();
    }
}

static void test_controller_assign_knob_to_unallocated(void)
{
    FakeThrottleBackend backend;
    ThrottleController controller(&backend);
    UiCallbackState uiState;

    controller.setUIUpdateCallback(uiUpdateCallback, &uiState);

    controller.onKnobIndicatorTouched(0, 0);

    Throttle* throttle = controller.getThrottle(0);
    Knob* knob = controller.getKnob(0);
    TEST_ASSERT_NOT_NULL(throttle);
    TEST_ASSERT_NOT_NULL(knob);

    TEST_ASSERT_EQUAL(Throttle::State::SELECTING, throttle->getState());
    TEST_ASSERT_EQUAL(Knob::State::SELECTING, knob->getState());
    TEST_ASSERT_EQUAL(0, knob->getAssignedThrottleId());
    TEST_ASSERT_GREATER_THAN(0, uiState.calls);
}

static void test_controller_move_knob_between_throttles(void)
{
    FakeThrottleBackend backend;
    ThrottleController controller(&backend);

    setupThrottleWithLoco(controller, 0, 0, "LocoA", 10);
    setupThrottleAllocatedNoKnob(controller, 1, 1, "LocoB", 20);

    controller.onKnobIndicatorTouched(1, 0);

    Throttle* throttle0 = controller.getThrottle(0);
    Throttle* throttle1 = controller.getThrottle(1);

    TEST_ASSERT_NOT_NULL(throttle0);
    TEST_ASSERT_NOT_NULL(throttle1);

    TEST_ASSERT_EQUAL(Throttle::State::ALLOCATED_NO_KNOB, throttle0->getState());
    TEST_ASSERT_EQUAL(Throttle::State::ALLOCATED_WITH_KNOB, throttle1->getState());
    TEST_ASSERT_EQUAL(0, throttle1->getAssignedKnob());
}

static void test_controller_move_knob_to_unallocated_for_selection(void)
{
    FakeThrottleBackend backend;
    ThrottleController controller(&backend);

    setupThrottleWithLoco(controller, 0, 0, "LocoA", 10);

    controller.onKnobIndicatorTouched(1, 0);

    Throttle* throttle0 = controller.getThrottle(0);
    Throttle* throttle1 = controller.getThrottle(1);
    Knob* knob0 = controller.getKnob(0);

    TEST_ASSERT_NOT_NULL(throttle0);
    TEST_ASSERT_NOT_NULL(throttle1);
    TEST_ASSERT_NOT_NULL(knob0);

    TEST_ASSERT_EQUAL(Throttle::State::ALLOCATED_NO_KNOB, throttle0->getState());
    TEST_ASSERT_EQUAL(Throttle::State::SELECTING, throttle1->getState());
    TEST_ASSERT_EQUAL(Knob::State::SELECTING, knob0->getState());
    TEST_ASSERT_EQUAL(1, knob0->getAssignedThrottleId());
}

static void test_controller_release_resets_knob(void)
{
    FakeThrottleBackend backend;
    ThrottleController controller(&backend);
    UiCallbackState uiState;

    controller.setUIUpdateCallback(uiUpdateCallback, &uiState);
    setupThrottleWithLoco(controller, 0, 0, "LocoC", 30);

    controller.onThrottleRelease(0);

    Throttle* throttle = controller.getThrottle(0);
    Knob* knob = controller.getKnob(0);
    TEST_ASSERT_NOT_NULL(throttle);
    TEST_ASSERT_NOT_NULL(knob);

    TEST_ASSERT_EQUAL(Throttle::State::UNALLOCATED, throttle->getState());
    TEST_ASSERT_EQUAL(Knob::State::IDLE, knob->getState());
    TEST_ASSERT_GREATER_THAN(0, uiState.calls);
}

static void test_controller_rotation_updates_speed(void)
{
    FakeThrottleBackend backend;
    ThrottleController controller(&backend);

    setupThrottleWithLoco(controller, 0, 0, "LocoD", 40);

    Throttle* throttle = controller.getThrottle(0);
    TEST_ASSERT_NOT_NULL(throttle);

    throttle->setSpeed(0);
    controller.onKnobRotation(0, 1);

    int speed = throttle->getCurrentSpeed();
    TEST_ASSERT_GREATER_THAN_INT(0, speed);
    TEST_ASSERT_LESS_OR_EQUAL_INT(126, speed);
}

static void test_controller_rotation_cross_zero_switches_to_reverse(void)
{
    FakeThrottleBackend backend;
    ThrottleController controller(&backend);

    setupThrottleWithLoco(controller, 0, 0, "LocoE", 50);

    Throttle* throttle = controller.getThrottle(0);
    TEST_ASSERT_NOT_NULL(throttle);

    throttle->setSpeed(4);
    throttle->setDirection(true);

    controller.onKnobRotation(0, -2); // 4 + (-2*4) = -4

    TEST_ASSERT_EQUAL_INT(4, throttle->getCurrentSpeed());
    TEST_ASSERT_FALSE(throttle->getDirection());
}

static void test_controller_rotation_cross_zero_switches_to_forward(void)
{
    FakeThrottleBackend backend;
    ThrottleController controller(&backend);

    setupThrottleWithLoco(controller, 0, 0, "LocoF", 60);

    Throttle* throttle = controller.getThrottle(0);
    TEST_ASSERT_NOT_NULL(throttle);

    throttle->setSpeed(8);
    throttle->setDirection(false);

    controller.onKnobRotation(0, 3); // -8 + (3*4) = 4

    TEST_ASSERT_EQUAL_INT(4, throttle->getCurrentSpeed());
    TEST_ASSERT_TRUE(throttle->getDirection());
}

// --- Seam tests -----------------------------------------------------------
// These assert the controller talks to the port correctly. The id checks
// matter most: the controller used to send WiThrottle's '0' + id character,
// and a backend that took the int literally would drive throttle 48.

static void test_controller_sends_speed_to_backend(void)
{
    FakeThrottleBackend backend;
    ThrottleController controller(&backend);

    setupThrottleWithLoco(controller, 2, 1, "LocoG", 70);

    Throttle* throttle = controller.getThrottle(2);
    TEST_ASSERT_NOT_NULL(throttle);
    throttle->setSpeed(0);

    controller.onKnobRotation(1, 1);

    TEST_ASSERT_EQUAL_INT(1, (int)backend.speeds.size());
    TEST_ASSERT_EQUAL_INT(2, backend.speeds[0].throttleId);
    TEST_ASSERT_EQUAL_INT(throttle->getCurrentSpeed(), backend.speeds[0].intArg);
}

static void test_controller_release_reaches_backend_by_index(void)
{
    FakeThrottleBackend backend;
    ThrottleController controller(&backend);

    setupThrottleWithLoco(controller, 3, 0, "LocoH", 80);

    controller.onThrottleRelease(3);

    TEST_ASSERT_EQUAL_INT(1, (int)backend.releases.size());
    TEST_ASSERT_EQUAL_INT(3, backend.releases[0].throttleId);
}

static void test_controller_applies_backend_throttle_update(void)
{
    FakeThrottleBackend backend;
    ThrottleController controller(&backend);

    setupThrottleWithLoco(controller, 1, 0, "LocoI", 90);

    ThrottleBackend::ThrottleUpdate update;
    update.throttleId = 1;
    update.address = 90;
    update.speed = 42;
    update.direction = 0;
    backend.emitThrottleUpdate(update);

    Throttle* throttle = controller.getThrottle(1);
    TEST_ASSERT_NOT_NULL(throttle);
    TEST_ASSERT_EQUAL_INT(42, throttle->getCurrentSpeed());
    TEST_ASSERT_FALSE(throttle->getDirection());
}

static void test_controller_roster_comes_from_backend(void)
{
    FakeThrottleBackend backend;
    backend.roster.push_back({12, "Pannier", false});
    backend.roster.push_back({4472, "Flying Scotsman", true});

    ThrottleController controller(&backend);

    TEST_ASSERT_EQUAL_INT(2, (int)controller.getRosterSize());

    ThrottleBackend::RosterEntry entry;
    TEST_ASSERT_TRUE(controller.getLocoAtRosterIndex(1, entry));
    TEST_ASSERT_EQUAL_INT(4472, entry.address);
    TEST_ASSERT_TRUE(entry.longAddress);
    TEST_ASSERT_EQUAL_STRING("Flying Scotsman", entry.name.c_str());

    TEST_ASSERT_FALSE(controller.getLocoAtRosterIndex(2, entry));
}

static void test_controller_hides_roster_when_backend_has_none(void)
{
    FakeThrottleBackend backend;
    backend.rosterProvided = false;
    backend.roster.push_back({12, "Pannier", false});

    ThrottleController controller(&backend);

    // The capability, not the vector, is what the controller must believe.
    TEST_ASSERT_EQUAL_INT(0, (int)controller.getRosterSize());

    ThrottleBackend::RosterEntry entry;
    TEST_ASSERT_FALSE(controller.getLocoAtRosterIndex(0, entry));
}

extern "C" void register_controller_tests(void)
{
    RUN_TEST(test_controller_assign_knob_to_unallocated);
    RUN_TEST(test_controller_move_knob_between_throttles);
    RUN_TEST(test_controller_move_knob_to_unallocated_for_selection);
    RUN_TEST(test_controller_release_resets_knob);
    RUN_TEST(test_controller_rotation_updates_speed);
    RUN_TEST(test_controller_rotation_cross_zero_switches_to_reverse);
    RUN_TEST(test_controller_rotation_cross_zero_switches_to_forward);
    RUN_TEST(test_controller_sends_speed_to_backend);
    RUN_TEST(test_controller_release_reaches_backend_by_index);
    RUN_TEST(test_controller_applies_backend_throttle_update);
    RUN_TEST(test_controller_roster_comes_from_backend);
    RUN_TEST(test_controller_hides_roster_when_backend_has_none);
}
