#include "unity.h"
#include "Roster.h"
#include "Locomotive.h"

static void test_roster_add_find_and_clear(void)
{
    Roster roster;
    TEST_ASSERT_TRUE(roster.isEmpty());

    TEST_ASSERT_TRUE(roster.addLocomotive("Loco1", 3, Locomotive::AddressType::SHORT));
    TEST_ASSERT_TRUE(roster.addLocomotive("Loco2", 300, Locomotive::AddressType::LONG));

    TEST_ASSERT_EQUAL(2, roster.getCount());
    TEST_ASSERT_FALSE(roster.isEmpty());

    TEST_ASSERT_EQUAL(0, roster.findByName("Loco1"));
    TEST_ASSERT_EQUAL(1, roster.findByAddress(300, Locomotive::AddressType::LONG));
    TEST_ASSERT_EQUAL(-1, roster.findByName("Missing"));

    roster.clear();
    TEST_ASSERT_TRUE(roster.isEmpty());
}

static void test_roster_navigation_wraps(void)
{
    Roster roster;
    roster.addLocomotive("A", 1, Locomotive::AddressType::SHORT);
    roster.addLocomotive("B", 2, Locomotive::AddressType::SHORT);

    TEST_ASSERT_EQUAL(1, roster.getNextIndex(0));
    TEST_ASSERT_EQUAL(0, roster.getNextIndex(1));

    TEST_ASSERT_EQUAL(1, roster.getPreviousIndex(0));
    TEST_ASSERT_EQUAL(0, roster.getPreviousIndex(1));
}

static void test_roster_create_copy(void)
{
    Roster roster;
    roster.addLocomotive("Copy", 12, Locomotive::AddressType::SHORT);
    const Locomotive* loco = roster.getLocomotive(0);
    TEST_ASSERT_NOT_NULL(loco);

    std::unique_ptr<Locomotive> copy = roster.createLocomotiveCopy(0);
    TEST_ASSERT_NOT_NULL(copy.get());
    TEST_ASSERT_EQUAL_STRING(loco->getName().c_str(), copy->getName().c_str());
    TEST_ASSERT_EQUAL(loco->getAddress(), copy->getAddress());
    TEST_ASSERT_EQUAL((int)loco->getAddressType(), (int)copy->getAddressType());
}

extern "C" void register_roster_tests(void)
{
    RUN_TEST(test_roster_add_find_and_clear);
    RUN_TEST(test_roster_navigation_wraps);
    RUN_TEST(test_roster_create_copy);
}
