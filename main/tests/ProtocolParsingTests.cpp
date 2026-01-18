#include "unity.h"
#include "WiThrottleClient.h"
#include "JmriJsonClient.h"

static void test_withrottle_roster_parsing(void)
{
    WiThrottleClient client;
    client.initialize();

    bool rosterCalled = false;
    client.setRosterCallback([&](const std::vector<WiThrottleClient::Locomotive>& roster) {
        rosterCalled = true;
        TEST_ASSERT_EQUAL(2, roster.size());
        TEST_ASSERT_EQUAL_STRING("LocoA", roster[0].name.c_str());
        TEST_ASSERT_EQUAL(3, roster[0].address);
        TEST_ASSERT_EQUAL('S', roster[0].addressType);
    });

    // RL2]\[LocoA}|{3}|{S]\[LocoB}|{40}|{L
    client.testProcessMessage("RL2]\\[LocoA}|{3}|{S]\\[LocoB}|{40}|{L");

    TEST_ASSERT_TRUE(rosterCalled);
    WiThrottleClient::Locomotive entry;
    TEST_ASSERT_TRUE(client.getRosterEntry(1, entry));
    TEST_ASSERT_EQUAL_STRING("LocoB", entry.name.c_str());
    TEST_ASSERT_EQUAL(40, entry.address);
    TEST_ASSERT_EQUAL('L', entry.addressType);
}

static void test_withrottle_throttle_update_parsing(void)
{
    WiThrottleClient client;
    client.initialize();

    bool updateCalled = false;
    client.setThrottleStateCallback([&](const WiThrottleClient::ThrottleUpdate& update) {
        updateCalled = true;
        TEST_ASSERT_EQUAL('0', update.throttleId);
        TEST_ASSERT_EQUAL(3, update.address);
        TEST_ASSERT_EQUAL(50, update.speed);
    });

    client.testProcessMessage("M0AS3<;>V50");
    TEST_ASSERT_TRUE(updateCalled);
}

static void test_jmri_power_parsing(void)
{
    JmriJsonClient client;
    client.initialize();
    client.setConfiguredPowerName("main");

    bool powerCalled = false;
    client.setPowerStateCallback([&](const std::string& name, JmriJsonClient::PowerState state) {
        powerCalled = true;
        TEST_ASSERT_EQUAL_STRING("main", name.c_str());
        TEST_ASSERT_EQUAL((int)JmriJsonClient::PowerState::ON, (int)state);
    });

    client.testProcessMessage("{\"type\":\"power\",\"data\":{\"name\":\"main\",\"state\":2}}");
    TEST_ASSERT_TRUE(powerCalled);
    TEST_ASSERT_EQUAL((int)JmriJsonClient::PowerState::ON, (int)client.getPower());
}

extern "C" void register_protocol_tests(void)
{
    RUN_TEST(test_withrottle_roster_parsing);
    RUN_TEST(test_withrottle_throttle_update_parsing);
    RUN_TEST(test_jmri_power_parsing);
}
