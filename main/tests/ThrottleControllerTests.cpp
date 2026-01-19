#include "unity.h"
#include "ThrottleController.h"
#include "WiThrottleClient.h"
#include "Locomotive.h"

namespace {
    struct UiCallbackState {
        int calls = 0;
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
    WiThrottleClient client;
    ThrottleController controller(&client);
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
    WiThrottleClient client;
    ThrottleController controller(&client);

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
    WiThrottleClient client;
    ThrottleController controller(&client);

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
    WiThrottleClient client;
    ThrottleController controller(&client);
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
    WiThrottleClient client;
    ThrottleController controller(&client);

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
    WiThrottleClient client;
    ThrottleController controller(&client);

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
    WiThrottleClient client;
    ThrottleController controller(&client);

    setupThrottleWithLoco(controller, 0, 0, "LocoF", 60);

    Throttle* throttle = controller.getThrottle(0);
    TEST_ASSERT_NOT_NULL(throttle);

    throttle->setSpeed(8);
    throttle->setDirection(false);

    controller.onKnobRotation(0, 3); // -8 + (3*4) = 4

    TEST_ASSERT_EQUAL_INT(4, throttle->getCurrentSpeed());
    TEST_ASSERT_TRUE(throttle->getDirection());
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
}
