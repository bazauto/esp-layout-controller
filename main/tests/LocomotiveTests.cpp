#include "unity.h"
#include "Locomotive.h"

static void test_locomotive_address_string(void)
{
    Locomotive shortLoco("Short", 12, Locomotive::AddressType::SHORT);
    Locomotive longLoco("Long", 300, Locomotive::AddressType::LONG);

    TEST_ASSERT_EQUAL_STRING("S12", shortLoco.getAddressString().c_str());
    TEST_ASSERT_EQUAL_STRING("L300", longLoco.getAddressString().c_str());
}

static void test_locomotive_speed_clamps(void)
{
    Locomotive loco("Speed", 1, Locomotive::AddressType::SHORT);
    loco.setSpeed(200);
    TEST_ASSERT_EQUAL(126, loco.getSpeed());
}

static void test_locomotive_functions(void)
{
    Locomotive loco("Func", 2, Locomotive::AddressType::SHORT);

    loco.setFunctionLabel(0, "Headlight");
    loco.setFunctionState(0, true);
    TEST_ASSERT_TRUE(loco.getFunctionState(0));
    TEST_ASSERT_EQUAL_STRING("Headlight", loco.getFunctionLabel(0).c_str());

    TEST_ASSERT_FALSE(loco.getFunctionState(28));
    TEST_ASSERT_EQUAL_STRING("", loco.getFunctionLabel(28).c_str());
}

extern "C" void register_locomotive_tests(void)
{
    RUN_TEST(test_locomotive_address_string);
    RUN_TEST(test_locomotive_speed_clamps);
    RUN_TEST(test_locomotive_functions);
}
