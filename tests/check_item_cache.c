/* Copyright (c) 2008 The Kaphan Foundation
 *
 * Possession of a copy of this file grants no permission or license
 * to use, modify, or create derivate works.
 *
 * Please contact info@peerworks.org for further information.
 */

#include <check.h>
#include "assertions.h"
#include "../src/item_cache.h"
#include "../src/misc.h"
#include "../src/item_cache.h"

START_TEST (creating_with_missing_db_file_fails) {
  ItemCache *item_cache;
  int rc = item_cache_create(&item_cache, "missing.db");
  assert_equal(CLASSIFIER_FAIL, rc);
  const char *msg = item_cache_errmsg(item_cache);
  assert_equal_s("unable to open database file", msg);
} END_TEST

START_TEST (creating_with_empty_db_file_fails) {
  ItemCache *item_cache;
  int rc = item_cache_create(&item_cache, "fixtures/empty.db");
  assert_equal(CLASSIFIER_FAIL, rc);
  const char *msg = item_cache_errmsg(item_cache); 
  assert_equal_s("Database file's user version does not match classifier version. Trying running classifier-db-migrate.", msg);           
} END_TEST

START_TEST (create_with_valid_db) {
  ItemCache *item_cache;
  int rc = item_cache_create(&item_cache, "fixtures/valid.db");
  assert_equal(CLASSIFIER_OK, rc);
} END_TEST

/* Tests for fetching an item */
ItemCache *item_cache;

static void setup_cache(void) {
  item_cache_create(&item_cache, "fixtures/valid.db");
}

static void teardown_item_cache(void) {
  free_item_cache(item_cache);
}

START_TEST (test_fetch_item_returns_null_when_item_doesnt_exist) {
  Item *item = item_cache_fetch_item(item_cache, 111);
  assert_null(item);
  free_item(item);
} END_TEST

START_TEST (test_fetch_item_contains_item_id) {
  Item *item = item_cache_fetch_item(item_cache, 890806);
  assert_not_null(item);
  assert_equal(890806, item_get_id(item));
  free_item(item);
} END_TEST

START_TEST (test_fetch_item_contains_item_time) {
  Item *item = item_cache_fetch_item(item_cache, 890806);
  assert_not_null(item);
  assert_equal(1178551672, item_get_time(item));
  free_item(item);
} END_TEST

START_TEST (test_fetch_item_contains_the_right_number_of_tokens) {
 Item *item = item_cache_fetch_item(item_cache, 890806);
 assert_not_null(item);
 assert_equal(76, item_get_num_tokens(item));
 free_item(item);
} END_TEST

START_TEST (test_fetch_item_contains_the_right_frequency_for_a_given_token) {
  Item *item = item_cache_fetch_item(item_cache, 890806);
  assert_not_null(item);
  Token token;
  item_get_token(item, 9949, &token);
  assert_equal(9949, token.id);
  assert_equal(3, token.frequency);
} END_TEST

START_TEST (test_fetch_item_after_load) {
  item_cache_load(item_cache);
  Item *item = item_cache_fetch_item(item_cache, 890806);
  assert_not_null(item);
} END_TEST

START_TEST (test_fetch_item_after_load_contains_tokens) {
  item_cache_load(item_cache);
  Item *item = item_cache_fetch_item(item_cache, 890806);
  assert_not_null(item);
  assert_equal(76, item_get_num_tokens(item));
} END_TEST

/* Test loading the item cache */
START_TEST (test_load_loads_the_right_number_of_items) {
  int rc = item_cache_load(item_cache);
  assert_equal(CLASSIFIER_OK, rc);
  assert_equal(10, item_cache_cached_size(item_cache));
} END_TEST

START_TEST (test_load_sets_cache_loaded_to_true) {
  int rc = item_cache_load(item_cache);
  assert_equal(CLASSIFIER_OK, rc);
  assert_equal(true, item_cache_loaded(item_cache));
} END_TEST

/* Test iteration */
void setup_iteration(void) {
  item_cache_create(&item_cache, "fixtures/valid.db");
  item_cache_load(item_cache);
}

void teardown_iteration(void) {
  free_item_cache(item_cache);
}

int iterates_over_all_items(const Item *item, void *memo) {
  if (item) {
    int *iteration_count = (int*) memo;
    (*iteration_count)++;
  }
  
  return CLASSIFIER_OK;
}

START_TEST (test_iterates_over_all_items) {
  int iteration_count = 0;
  item_cache_each_item(item_cache, iterates_over_all_items, &iteration_count);
  assert_equal(10, iteration_count);
} END_TEST

int cancels_iteration_when_it_returns_fail(const Item *item, void *memo) {
  int *iteration_count = (int*) memo;
  (*iteration_count)++;
  
  if (*iteration_count < 5) {
    return CLASSIFIER_OK;
  } else {
    return CLASSIFIER_FAIL;
  }
}

START_TEST (test_iteration_stops_when_iterator_returns_CLASSIFIER_FAIL) {
  int iteration_count = 0;
  item_cache_each_item(item_cache, cancels_iteration_when_it_returns_fail, &iteration_count);
  assert_equal(5, iteration_count);
} END_TEST

static int i = 0;

int stores_ids(const Item * item, void *memo) {
  int *ids = (int*) memo;
  ids[i++] = item_get_id(item);
  return CLASSIFIER_OK;
}

START_TEST (test_iteration_happens_in_reverse_updated_order) {
  i = 0;
  int ids[10];
  item_cache_each_item(item_cache, stores_ids, ids);
  assert_equal(709254, ids[0]);
  assert_equal(880389, ids[1]);
  assert_equal(888769, ids[2]);
  assert_equal(886643, ids[3]);
  assert_equal(890806, ids[4]);
  assert_equal(802739, ids[5]);
  assert_equal(884409, ids[6]);
  assert_equal(753459, ids[7]);
  assert_equal(878944, ids[8]);
  assert_equal(886294, ids[9]);
} END_TEST

/* Test RandomBackground */
START_TEST (test_random_background_is_null_before_load) {
  assert_null(item_cache_random_background(item_cache));
} END_TEST

START_TEST (test_creates_random_background_after_load) {
  item_cache_load(item_cache);
  assert_not_null(item_cache_random_background(item_cache));
} END_TEST

START_TEST (test_random_background_is_correct_size) {
  item_cache_load(item_cache);
  assert_equal(750, pool_num_tokens(item_cache_random_background(item_cache)));
} END_TEST

START_TEST (test_random_background_has_right_count_for_a_token) {
  item_cache_load(item_cache);
  const Pool *bg = item_cache_random_background(item_cache);
  assert_equal(2, pool_token_frequency(bg, 2515));
} END_TEST

/* Item Cache modification */

static void setup_modification(void) {
  system("cp fixtures/valid.db fixtures/valid-copy.db");
  item_cache_create(&item_cache, "fixtures/valid-copy.db");  
  item_cache_load(item_cache);
}

static void teardown_modification(void) {
  free_item_cache(item_cache);
}

//START_TEST (test_wait_with_nothing_changed_returns_ITEM_CACHE_UNCHANGED_immediately) {
//  int rc = item_cache_wait(item_cache);
//  assert_equal(ITEM_CACHE_UNCHANGED, rc);
//} END_TEST
//
//START_TEST (test_wait_with_something_in_the_queue_returns_ITEM_CACHE_CHANGED) {
//  ItemCacheEntry *entry = create_entry();
//  item_cache_add_entry(item_cache, entry);
//  int rc = item_cache_wait(item_cache);
//  assert_equal(ITEM_CACHE_CHANGED, rc);
//} END_TEST

START_TEST (test_adding_an_entry_stores_it_in_the_database) {
  ItemCacheEntry *entry = create_item_cache_entry(11, "id#11", "Entry 11", "Author 11",
                                        "http://example.org/11",
                                        "http://example.org/11.html",
                                        "<p>This is some content</p>",
                                        2000.0002, 1, 20001.2);
  int rc = item_cache_add_entry(item_cache, entry);
  assert_equal(CLASSIFIER_OK, rc);
  Item *item = item_cache_fetch_item(item_cache, 11);
  assert_not_null(item);
  free_item(item);
} END_TEST


Suite *
item_cache_suite(void) {
  Suite *s = suite_create("ItemCache");  
  TCase *tc_case = tcase_create("case");

// START_TESTS
  tcase_add_test(tc_case, creating_with_missing_db_file_fails);
  tcase_add_test(tc_case, creating_with_empty_db_file_fails);
  tcase_add_test(tc_case, create_with_valid_db);
    
// END_TESTS
  
  TCase *fetch_item_case = tcase_create("fetch_item");
  tcase_add_checked_fixture(fetch_item_case, setup_cache, teardown_item_cache);
  tcase_add_test(fetch_item_case, test_fetch_item_returns_null_when_item_doesnt_exist);
  tcase_add_test(fetch_item_case, test_fetch_item_contains_item_id);
  tcase_add_test(fetch_item_case, test_fetch_item_contains_item_time);
  tcase_add_test(fetch_item_case, test_fetch_item_contains_the_right_number_of_tokens);
  tcase_add_test(fetch_item_case, test_fetch_item_contains_the_right_frequency_for_a_given_token);
  tcase_add_test(fetch_item_case, test_fetch_item_after_load);
  tcase_add_test(fetch_item_case, test_fetch_item_after_load_contains_tokens);
  
  TCase *load = tcase_create("load");
  tcase_add_checked_fixture(load, setup_cache, teardown_item_cache);
  tcase_add_test(load, test_load_loads_the_right_number_of_items);  
  tcase_add_test(load, test_load_sets_cache_loaded_to_true);
  
  TCase *iteration = tcase_create("iteration");
  tcase_add_checked_fixture(iteration, setup_iteration, teardown_iteration);
  tcase_add_test(iteration, test_iterates_over_all_items);
  tcase_add_test(iteration, test_iteration_stops_when_iterator_returns_CLASSIFIER_FAIL);
  tcase_add_test(iteration, test_iteration_happens_in_reverse_updated_order);
  
  TCase *rndbg = tcase_create("random background");
  tcase_add_checked_fixture(rndbg, setup_cache, teardown_item_cache);
  tcase_add_test(rndbg, test_random_background_is_null_before_load);
  tcase_add_test(rndbg, test_creates_random_background_after_load);
  tcase_add_test(rndbg, test_random_background_is_correct_size);
  tcase_add_test(rndbg, test_random_background_has_right_count_for_a_token);
  
  TCase *modification = tcase_create("modification");
  tcase_add_checked_fixture(modification, setup_modification, teardown_modification);
  tcase_add_test(modification, test_adding_an_entry_stores_it_in_the_database);
  
  suite_add_tcase(s, tc_case);
  suite_add_tcase(s, fetch_item_case);
  suite_add_tcase(s, load);
  suite_add_tcase(s, iteration);
  suite_add_tcase(s, rndbg);
  suite_add_tcase(s, modification);
  return s;
}

