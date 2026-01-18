#include "unity.h"
#include "Throttle.h"
#include "Knob.h"

static void test_throttle_assign_release(void)
{
    Throttle throttle(1);
    TEST_ASSERT_EQUAL(Throttle::State::UNALLOCATED, throttle.getState());
    TEST_ASSERT_FALSE(throttle.hasLocomotive());

    auto loco = std::make_unique<Locomotive>("Test Loco", 42, Locomotive::AddressType::SHORT);
    TEST_ASSERT_FALSE(throttle.assignLocomotive(std::move(loco)));

    TEST_ASSERT_TRUE(throttle.assignKnob(Throttle::KNOB_1));
    TEST_ASSERT_EQUAL(Throttle::State::SELECTING, throttle.getState());

    auto loco2 = std::make_unique<Locomotive>("Test Loco", 42, Locomotive::AddressType::SHORT);
    TEST_ASSERT_TRUE(throttle.assignLocomotive(std::move(loco2)));
    TEST_ASSERT_EQUAL(Throttle::State::ALLOCATED_WITH_KNOB, throttle.getState());
    TEST_ASSERT_TRUE(throttle.hasLocomotive());

    throttle.unassignKnob();
    TEST_ASSERT_EQUAL(Throttle::State::ALLOCATED_NO_KNOB, throttle.getState());

    auto released = throttle.releaseLocomotive();
    TEST_ASSERT_NOT_NULL(released.get());
    TEST_ASSERT_EQUAL(Throttle::State::UNALLOCATED, throttle.getState());
}

static void test_knob_assignment_flow(void)
{
    Knob knob(0);
    TEST_ASSERT_EQUAL(Knob::State::IDLE, knob.getState());

    knob.assignToThrottle(2);
    TEST_ASSERT_EQUAL(Knob::State::SELECTING, knob.getState());
    TEST_ASSERT_EQUAL(2, knob.getAssignedThrottleId());

    knob.startControlling();
    TEST_ASSERT_EQUAL(Knob::State::CONTROLLING, knob.getState());

    knob.release();
    TEST_ASSERT_EQUAL(Knob::State::IDLE, knob.getState());
    TEST_ASSERT_EQUAL(-1, knob.getAssignedThrottleId());
}

static void test_knob_rotation_wrap(void)
{
    Knob knob(1);
    knob.assignToThrottle(0);

    knob.handleRotation(1, 5);
    TEST_ASSERT_EQUAL(1, knob.getRosterIndex());

    knob.handleRotation(-2, 5);
    TEST_ASSERT_EQUAL(4, knob.getRosterIndex());

    knob.handleRotation(7, 5);
    TEST_ASSERT_EQUAL(1, knob.getRosterIndex());
}

static void test_throttle_speed_clamp(void)
{
    Throttle throttle(0);
    TEST_ASSERT_TRUE(throttle.assignKnob(Throttle::KNOB_1));

    auto loco = std::make_unique<Locomotive>("Clamp Loco", 7, Locomotive::AddressType::SHORT);
    TEST_ASSERT_TRUE(throttle.assignLocomotive(std::move(loco)));

    throttle.setSpeed(-5);
    TEST_ASSERT_EQUAL(0, throttle.getCurrentSpeed());

    throttle.setSpeed(200);
    TEST_ASSERT_EQUAL(126, throttle.getCurrentSpeed());
}

static void test_throttle_function_state_updates(void)
{
    Throttle throttle(2);
    throttle.addFunction(Function(1, "Bell", false));
    throttle.addFunction(Function(2, "Horn", false));

    throttle.setFunctionState(1, true);
    const auto& functions = throttle.getFunctions();
    TEST_ASSERT_EQUAL(2, functions.size());
    TEST_ASSERT_TRUE(functions[0].state);
    TEST_ASSERT_FALSE(functions[1].state);
}

static void test_knob_rotation_ignored_when_idle(void)
{
    Knob knob(0);
    TEST_ASSERT_EQUAL(Knob::State::IDLE, knob.getState());

    knob.handleRotation(3, 10);
    TEST_ASSERT_EQUAL(0, knob.getRosterIndex());
}

static void test_throttle_assign_knob_invalid(void)
{
    Throttle throttle(3);
    TEST_ASSERT_FALSE(throttle.assignKnob(5));
    TEST_ASSERT_EQUAL(Throttle::State::UNALLOCATED, throttle.getState());
    TEST_ASSERT_EQUAL(Throttle::KNOB_NONE, throttle.getAssignedKnob());
}

static void test_throttle_unassign_transitions(void)
{
    Throttle throttle(0);
    TEST_ASSERT_TRUE(throttle.assignKnob(Throttle::KNOB_1));
    TEST_ASSERT_EQUAL(Throttle::State::SELECTING, throttle.getState());

    throttle.unassignKnob();
    TEST_ASSERT_EQUAL(Throttle::State::UNALLOCATED, throttle.getState());

    TEST_ASSERT_TRUE(throttle.assignKnob(Throttle::KNOB_2));
    auto loco = std::make_unique<Locomotive>("State Loco", 99, Locomotive::AddressType::SHORT);
    TEST_ASSERT_TRUE(throttle.assignLocomotive(std::move(loco)));
    TEST_ASSERT_EQUAL(Throttle::State::ALLOCATED_WITH_KNOB, throttle.getState());

    throttle.unassignKnob();
    TEST_ASSERT_EQUAL(Throttle::State::ALLOCATED_NO_KNOB, throttle.getState());
}

static void test_throttle_release_resets_state(void)
{
    Throttle throttle(1);
    throttle.setSpeed(50);
    throttle.setDirection(false);
    throttle.addFunction(Function(1, "F1", true));

    TEST_ASSERT_TRUE(throttle.assignKnob(Throttle::KNOB_1));
    auto loco = std::make_unique<Locomotive>("Reset Loco", 12, Locomotive::AddressType::SHORT);
    TEST_ASSERT_TRUE(throttle.assignLocomotive(std::move(loco)));

    throttle.setSpeed(100);
    throttle.setDirection(false);
    throttle.setFunctionState(1, true);

    auto released = throttle.releaseLocomotive();
    TEST_ASSERT_NOT_NULL(released.get());
    TEST_ASSERT_EQUAL(Throttle::State::UNALLOCATED, throttle.getState());
    TEST_ASSERT_FALSE(throttle.hasLocomotive());
    TEST_ASSERT_EQUAL(0, throttle.getCurrentSpeed());
    TEST_ASSERT_TRUE(throttle.getDirection());
    TEST_ASSERT_EQUAL(0, throttle.getFunctions().size());
}

static void test_throttle_assign_loco_requires_selecting(void)
{
    Throttle throttle(2);
    auto loco = std::make_unique<Locomotive>("Invalid", 5, Locomotive::AddressType::SHORT);
    TEST_ASSERT_FALSE(throttle.assignLocomotive(std::move(loco)));
    TEST_ASSERT_EQUAL(Throttle::State::UNALLOCATED, throttle.getState());
}

static void test_knob_reassign_overwrites_previous(void)
{
    Knob knob(0);
    knob.assignToThrottle(1);
    TEST_ASSERT_EQUAL(1, knob.getAssignedThrottleId());

    knob.assignToThrottle(3);
    TEST_ASSERT_EQUAL(Knob::State::SELECTING, knob.getState());
    TEST_ASSERT_EQUAL(3, knob.getAssignedThrottleId());
    TEST_ASSERT_EQUAL(0, knob.getRosterIndex());
}

static void test_knob_rotation_ignored_when_no_roster(void)
{
    Knob knob(1);
    knob.assignToThrottle(0);
    knob.handleRotation(3, 0);
    TEST_ASSERT_EQUAL(0, knob.getRosterIndex());
}

void register_throttle_tests(void)
{
    RUN_TEST(test_throttle_assign_release);
    RUN_TEST(test_knob_assignment_flow);
    RUN_TEST(test_knob_rotation_wrap);
    RUN_TEST(test_throttle_speed_clamp);
    RUN_TEST(test_throttle_function_state_updates);
    RUN_TEST(test_knob_rotation_ignored_when_idle);
    RUN_TEST(test_throttle_assign_knob_invalid);
    RUN_TEST(test_throttle_unassign_transitions);
    RUN_TEST(test_throttle_release_resets_state);
    RUN_TEST(test_throttle_assign_loco_requires_selecting);
    RUN_TEST(test_knob_reassign_overwrites_previous);
    RUN_TEST(test_knob_rotation_ignored_when_no_roster);
}
