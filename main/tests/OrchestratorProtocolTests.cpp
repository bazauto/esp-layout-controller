#include "unity.h"

#include <string>
#include <vector>

#include "OrchestratorBackend.h"
#include "OrchestratorClient.h"

/**
 * Tests for the orchestrator control-plane parser.
 *
 * The point of most of these is *refusal*. JmriJsonClient parses JSON by
 * substring search, which is adequate for the narrow JMRI subset it reads. It
 * is not adequate here: a malformed orchestrator payload must be rejected
 * outright rather than half-parsed, because the half that survives would become
 * a speed command on real track.
 */

namespace {

struct Captured {
    std::vector<OrchestratorClient::LocoState> locoStates;
    int systemStatusCalls = 0;
    OrchestratorClient::SystemStatus lastStatus = OrchestratorClient::SystemStatus::UNKNOWN;
    std::string lastReason;
};

/** Wires a client up to record everything it dispatches. */
void attach(OrchestratorClient& client, Captured& captured)
{
    client.setLocoStateCallback([&captured](const OrchestratorClient::LocoState& state) {
        captured.locoStates.push_back(state);
    });
    client.setSystemStatusCallback(
        [&captured](OrchestratorClient::SystemStatus status, const std::string& reason) {
            captured.systemStatusCalls++;
            captured.lastStatus = status;
            captured.lastReason = reason;
        });
}

}  // namespace

// --- Set-Cookie parsing ----------------------------------------------------

static void test_orch_cookie_extracted_from_set_cookie(void)
{
    std::string token;
    TEST_ASSERT_TRUE(OrchestratorClient::extractSessionCookie(
        "layout_session=abc123def; Path=/; HttpOnly; SameSite=Lax", token));
    TEST_ASSERT_EQUAL_STRING("abc123def", token.c_str());
}

static void test_orch_cookie_stops_at_first_attribute(void)
{
    // The bug this guards: swallowing "; Path=/" into the token, which then
    // fails every subsequent request with a 401 that looks like bad credentials.
    std::string token;
    TEST_ASSERT_TRUE(OrchestratorClient::extractSessionCookie(
        "layout_session=tok; Path=/; Max-Age=2592000", token));
    TEST_ASSERT_EQUAL_STRING("tok", token.c_str());
}

static void test_orch_cookie_absent_is_refused(void)
{
    std::string token;
    TEST_ASSERT_FALSE(OrchestratorClient::extractSessionCookie(
        "other_cookie=value; Path=/", token));
    TEST_ASSERT_FALSE(OrchestratorClient::extractSessionCookie("", token));
}

static void test_orch_cookie_empty_value_is_refused(void)
{
    // A cleared cookie (logout) must not read as a valid session.
    std::string token;
    TEST_ASSERT_FALSE(OrchestratorClient::extractSessionCookie(
        "layout_session=; Path=/; Max-Age=0", token));
}

// --- LOCO_STATE: accepted --------------------------------------------------

static void test_orch_loco_state_applied(void)
{
    OrchestratorClient client;
    client.initialize();
    Captured captured;
    attach(client, captured);

    client.testHandleMessage(
        "{\"type\":\"LOCO_STATE\",\"payload\":{"
        "\"address\":4472,\"speed\":64,\"direction\":\"fwd\","
        "\"functions\":{\"0\":true,\"3\":false}}}");

    TEST_ASSERT_EQUAL_INT(1, (int)captured.locoStates.size());
    TEST_ASSERT_EQUAL_INT(4472, captured.locoStates[0].address);
    TEST_ASSERT_EQUAL_INT(64, captured.locoStates[0].speed);
    TEST_ASSERT_TRUE(captured.locoStates[0].direction ==
                     OrchestratorClient::Direction::FORWARD);
    TEST_ASSERT_TRUE(captured.locoStates[0].functions.at(0));
    TEST_ASSERT_FALSE(captured.locoStates[0].functions.at(3));
}

static void test_orch_loco_state_stop_direction(void)
{
    OrchestratorClient client;
    client.initialize();
    Captured captured;
    attach(client, captured);

    client.testHandleMessage(
        "{\"type\":\"LOCO_STATE\",\"payload\":"
        "{\"address\":12,\"speed\":0,\"direction\":\"stop\"}}");

    TEST_ASSERT_EQUAL_INT(1, (int)captured.locoStates.size());
    TEST_ASSERT_TRUE(captured.locoStates[0].direction == OrchestratorClient::Direction::STOP);
}

// --- LOCO_STATE: refused ---------------------------------------------------

static void test_orch_malformed_json_is_refused(void)
{
    OrchestratorClient client;
    client.initialize();
    Captured captured;
    attach(client, captured);

    client.testHandleMessage("{\"type\":\"LOCO_STATE\",\"payload\":{\"address\":12,");
    client.testHandleMessage("not json at all");
    client.testHandleMessage("");

    TEST_ASSERT_EQUAL_INT(0, (int)captured.locoStates.size());
}

static void test_orch_speed_out_of_range_is_refused(void)
{
    OrchestratorClient client;
    client.initialize();
    Captured captured;
    attach(client, captured);

    // 127 is one past the DCC step ceiling; a parser that clamped instead of
    // refusing would drive the loco flat out on a corrupt frame.
    client.testHandleMessage(
        "{\"type\":\"LOCO_STATE\",\"payload\":"
        "{\"address\":12,\"speed\":127,\"direction\":\"fwd\"}}");
    client.testHandleMessage(
        "{\"type\":\"LOCO_STATE\",\"payload\":"
        "{\"address\":12,\"speed\":-1,\"direction\":\"fwd\"}}");

    TEST_ASSERT_EQUAL_INT(0, (int)captured.locoStates.size());
}

static void test_orch_non_integer_speed_is_refused(void)
{
    OrchestratorClient client;
    client.initialize();
    Captured captured;
    attach(client, captured);

    // cJSON hands back every number as a double, so 64.7 would truncate to 64
    // without an explicit integrality check.
    client.testHandleMessage(
        "{\"type\":\"LOCO_STATE\",\"payload\":"
        "{\"address\":12,\"speed\":64.7,\"direction\":\"fwd\"}}");
    client.testHandleMessage(
        "{\"type\":\"LOCO_STATE\",\"payload\":"
        "{\"address\":12,\"speed\":\"64\",\"direction\":\"fwd\"}}");

    TEST_ASSERT_EQUAL_INT(0, (int)captured.locoStates.size());
}

static void test_orch_unknown_direction_is_refused(void)
{
    OrchestratorClient client;
    client.initialize();
    Captured captured;
    attach(client, captured);

    client.testHandleMessage(
        "{\"type\":\"LOCO_STATE\",\"payload\":"
        "{\"address\":12,\"speed\":10,\"direction\":\"sideways\"}}");
    client.testHandleMessage(
        "{\"type\":\"LOCO_STATE\",\"payload\":{\"address\":12,\"speed\":10}}");

    TEST_ASSERT_EQUAL_INT(0, (int)captured.locoStates.size());
}

static void test_orch_missing_address_is_refused(void)
{
    OrchestratorClient client;
    client.initialize();
    Captured captured;
    attach(client, captured);

    client.testHandleMessage(
        "{\"type\":\"LOCO_STATE\",\"payload\":{\"speed\":10,\"direction\":\"fwd\"}}");
    client.testHandleMessage(
        "{\"type\":\"LOCO_STATE\",\"payload\":"
        "{\"address\":0,\"speed\":10,\"direction\":\"fwd\"}}");

    TEST_ASSERT_EQUAL_INT(0, (int)captured.locoStates.size());
}

static void test_orch_frame_without_type_is_refused(void)
{
    OrchestratorClient client;
    client.initialize();
    Captured captured;
    attach(client, captured);

    client.testHandleMessage("{\"payload\":{\"address\":12,\"speed\":99,\"direction\":\"fwd\"}}");

    TEST_ASSERT_EQUAL_INT(0, (int)captured.locoStates.size());
}

// --- Other message types ---------------------------------------------------

static void test_orch_system_status_applied(void)
{
    OrchestratorClient client;
    client.initialize();
    Captured captured;
    attach(client, captured);

    client.testHandleMessage(
        "{\"type\":\"SYSTEM_STATUS\",\"payload\":"
        "{\"status\":\"safe-stop\",\"mode\":\"manual\",\"reason\":\"Sensor fault\"}}");

    TEST_ASSERT_EQUAL_INT(1, captured.systemStatusCalls);
    TEST_ASSERT_TRUE(captured.lastStatus == OrchestratorClient::SystemStatus::SAFE_STOP);
    TEST_ASSERT_EQUAL_STRING("Sensor fault", captured.lastReason.c_str());
    TEST_ASSERT_TRUE(client.getSystemStatus() == OrchestratorClient::SystemStatus::SAFE_STOP);
}

static void test_orch_unknown_system_status_is_refused(void)
{
    OrchestratorClient client;
    client.initialize();
    Captured captured;
    attach(client, captured);

    client.testHandleMessage(
        "{\"type\":\"SYSTEM_STATUS\",\"payload\":{\"status\":\"melting\"}}");

    TEST_ASSERT_EQUAL_INT(0, captured.systemStatusCalls);
    TEST_ASSERT_TRUE(client.getSystemStatus() == OrchestratorClient::SystemStatus::UNKNOWN);
}

static void test_orch_snapshot_applies_every_loco(void)
{
    OrchestratorClient client;
    client.initialize();
    Captured captured;
    attach(client, captured);

    client.testHandleMessage(
        "{\"type\":\"STATE_SNAPSHOT\",\"payload\":{"
        "\"systemStatus\":\"online\",\"safeStopReason\":null,"
        "\"locos\":{"
        "\"12\":{\"address\":12,\"speed\":30,\"direction\":\"fwd\"},"
        "\"4472\":{\"address\":4472,\"speed\":0,\"direction\":\"rev\"}}}}");

    TEST_ASSERT_EQUAL_INT(2, (int)captured.locoStates.size());
    TEST_ASSERT_EQUAL_INT(1, captured.systemStatusCalls);
    TEST_ASSERT_TRUE(captured.lastStatus == OrchestratorClient::SystemStatus::ONLINE);
}

static void test_orch_snapshot_skips_only_the_bad_loco(void)
{
    OrchestratorClient client;
    client.initialize();
    Captured captured;
    attach(client, captured);

    // One corrupt entry must not cost the whole snapshot, and must not itself
    // get through.
    client.testHandleMessage(
        "{\"type\":\"STATE_SNAPSHOT\",\"payload\":{"
        "\"systemStatus\":\"online\","
        "\"locos\":{"
        "\"12\":{\"address\":12,\"speed\":30,\"direction\":\"fwd\"},"
        "\"99\":{\"address\":99,\"speed\":999,\"direction\":\"fwd\"}}}}");

    TEST_ASSERT_EQUAL_INT(1, (int)captured.locoStates.size());
    TEST_ASSERT_EQUAL_INT(12, captured.locoStates[0].address);
}

static void test_orch_heartbeat_and_unknown_types_are_harmless(void)
{
    OrchestratorClient client;
    client.initialize();
    Captured captured;
    attach(client, captured);

    client.testHandleMessage(
        "{\"type\":\"HEARTBEAT\",\"payload\":{\"serverTime\":\"2026-08-27T13:00:00.000Z\"}}");
    client.testHandleMessage("{\"type\":\"BLOCK_STATE\",\"payload\":{\"blockId\":\"b1\"}}");
    client.testHandleMessage("{\"type\":\"ERROR\",\"payload\":{\"message\":\"nope\"}}");

    TEST_ASSERT_EQUAL_INT(0, (int)captured.locoStates.size());
    TEST_ASSERT_EQUAL_INT(0, captured.systemStatusCalls);
}

// --- Backend behaviour -----------------------------------------------------

static void test_orch_backend_capabilities(void)
{
    OrchestratorClient client;
    client.initialize();
    OrchestratorBackend backend(&client);

    // The orchestrator has no sessions and pushes state; both differences from
    // WiThrottle are declared here rather than faked.
    TEST_ASSERT_FALSE(backend.requiresAcquisition());
    TEST_ASSERT_FALSE(backend.requiresPolling());
    TEST_ASSERT_FALSE(backend.providesFunctionLabels());
    TEST_ASSERT_TRUE(backend.providesRoster());
}

static void test_orch_backend_refuses_commands_without_a_loco(void)
{
    OrchestratorClient client;
    client.initialize();
    OrchestratorBackend backend(&client);

    // Nothing acquired, so there is no address to command. Must refuse rather
    // than send a command naming loco 0.
    TEST_ASSERT_NOT_EQUAL(ESP_OK, backend.setSpeed(0, 50));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, backend.setDirection(0, true));
    TEST_ASSERT_NOT_EQUAL(ESP_OK, backend.setFunction(0, 1, true));
}

static void test_orch_backend_routes_state_to_matching_throttle(void)
{
    OrchestratorClient client;
    client.initialize();
    OrchestratorBackend backend(&client);

    std::vector<ThrottleBackend::ThrottleUpdate> updates;
    backend.setThrottleStateCallback(
        [&updates](const ThrottleBackend::ThrottleUpdate& u) { updates.push_back(u); });

    backend.acquireLocomotive(2, 4472, true);

    // A state for a loco this device is not driving must reach no throttle.
    client.testHandleMessage(
        "{\"type\":\"LOCO_STATE\",\"payload\":"
        "{\"address\":12,\"speed\":40,\"direction\":\"fwd\"}}");
    TEST_ASSERT_EQUAL_INT(0, (int)updates.size());

    client.testHandleMessage(
        "{\"type\":\"LOCO_STATE\",\"payload\":"
        "{\"address\":4472,\"speed\":40,\"direction\":\"rev\"}}");

    TEST_ASSERT_GREATER_THAN_INT(0, (int)updates.size());
    TEST_ASSERT_EQUAL_INT(2, updates[0].throttleId);
    TEST_ASSERT_EQUAL_INT(40, updates[0].speed);
    TEST_ASSERT_EQUAL_INT(0, updates[0].direction);
}

static void test_orch_backend_release_sends_nothing(void)
{
    OrchestratorClient client;
    client.initialize();
    OrchestratorBackend backend(&client);

    backend.acquireLocomotive(1, 12, false);
    TEST_ASSERT_EQUAL(ESP_OK, backend.releaseLocomotive(1));

    // After release the throttle commands nothing: the loco may still be under
    // another operator or an automation run, and stopping it because we stopped
    // looking at it would be a movement nobody asked for.
    TEST_ASSERT_NOT_EQUAL(ESP_OK, backend.setSpeed(1, 10));
}

extern "C" void register_orchestrator_tests(void)
{
    RUN_TEST(test_orch_cookie_extracted_from_set_cookie);
    RUN_TEST(test_orch_cookie_stops_at_first_attribute);
    RUN_TEST(test_orch_cookie_absent_is_refused);
    RUN_TEST(test_orch_cookie_empty_value_is_refused);
    RUN_TEST(test_orch_loco_state_applied);
    RUN_TEST(test_orch_loco_state_stop_direction);
    RUN_TEST(test_orch_malformed_json_is_refused);
    RUN_TEST(test_orch_speed_out_of_range_is_refused);
    RUN_TEST(test_orch_non_integer_speed_is_refused);
    RUN_TEST(test_orch_unknown_direction_is_refused);
    RUN_TEST(test_orch_missing_address_is_refused);
    RUN_TEST(test_orch_frame_without_type_is_refused);
    RUN_TEST(test_orch_system_status_applied);
    RUN_TEST(test_orch_unknown_system_status_is_refused);
    RUN_TEST(test_orch_snapshot_applies_every_loco);
    RUN_TEST(test_orch_snapshot_skips_only_the_bad_loco);
    RUN_TEST(test_orch_heartbeat_and_unknown_types_are_harmless);
    RUN_TEST(test_orch_backend_capabilities);
    RUN_TEST(test_orch_backend_refuses_commands_without_a_loco);
    RUN_TEST(test_orch_backend_routes_state_to_matching_throttle);
    RUN_TEST(test_orch_backend_release_sends_nothing);
}
