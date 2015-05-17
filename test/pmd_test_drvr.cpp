/*
 *  Copyright (C) 2015 Hewlett-Packard Development Company, L.P.
 *  All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License. You may obtain
 *  a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 *  License for the specific language governing permissions and limitations
 *  under the License.
 */

// The platform PMD unit test driver.

#include <gtest/gtest.h>

#include "pmd_test_fns.cpp"

// Define Test Suite class for customer setup and teardown
// functions.
class PMDTestSuite : public testing::Test
{
    void SetUp(void) {
      pmd_test_suite_setup();
    }

    void TearDown(void) {
      pmd_test_suite_tear_down();
    }
};

// Test Fixture.
TEST_F(PMDTestSuite, pmd_test_fixture1) {

    // clean startup
    pmd_test_clean_startup();

    // mismatch startup
    pmd_test_mismatch_startup();

    // change startup
    pmd_test_change_startup();

    // hot-plug
    pmd_test_hot_plug();

    // enable/disable
    pmd_test_enable_disable();

    // invalid checksum
    pmd_test_invalid_cksum();

    // HALON_TODO: add a2 test
}

//////////////////////////////////////////////////////
//
// The platform PMD test driver main function.
//
/////////////////////////////////////////////////////
GTEST_API_ int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
