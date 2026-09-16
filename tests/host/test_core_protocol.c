#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "velaguard_core.h"
#include "velaguard_protocol.h"

static int g_failures;

#define CHECK(condition)                                                     \
  do                                                                         \
    {                                                                        \
      if (!(condition))                                                      \
        {                                                                    \
          fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,          \
                  #condition);                                               \
          g_failures++;                                                      \
        }                                                                    \
    }                                                                        \
  while (0)

static vg_create_request_t request(const char *id, const char *title,
                                   int64_t due, int priority)
{
  vg_create_request_t value;

  memset(&value, 0, sizeof(value));
  snprintf(value.request_id, sizeof(value.request_id), "%s", id);
  snprintf(value.title, sizeof(value.title), "%s", title);
  value.due_epoch = due;
  value.priority = priority;
  return value;
}

static void test_create_and_deduplicate(void)
{
  vg_core_t core;
  vg_create_request_t first = request("demo-001", "submit", 1100, 1);
  size_t index = 99;

  vg_core_init(&core);
  CHECK(vg_core_create(&core, &first, 1000, true, &index) == VG_OK);
  CHECK(index == 0);
  CHECK(core.count == 1);
  CHECK(core.tasks[0].state == VG_TASK_CREATED);
  CHECK(vg_core_create(&core, &first, 1000, true, &index) ==
        VG_ERR_DUPLICATE_REQUEST);
  CHECK(index == 0);
  CHECK(core.count == 1);
}

static void test_time_capacity_and_transitions(void)
{
  vg_core_t core;
  vg_create_request_t item;
  size_t index;
  int i;

  vg_core_init(&core);
  item = request("bad-time", "bad", 1000, 1);
  CHECK(vg_core_create(&core, &item, 1000, true, &index) ==
        VG_ERR_INVALID_TIME);
  item.due_epoch = 2000;
  CHECK(vg_core_create(&core, &item, 1000, false, &index) ==
        VG_ERR_INVALID_TIME);

  for (i = 0; i < VG_MAX_ACTIVE_TASKS; i++)
    {
      char id[VG_REQUEST_ID_CAPACITY];
      snprintf(id, sizeof(id), "task-%02d", i);
      item = request(id, "capacity", 2000 + i, i % 3);
      CHECK(vg_core_create(&core, &item, 1000, true, &index) == VG_OK);
    }

  item = request("overflow", "capacity", 3000, 1);
  CHECK(vg_core_create(&core, &item, 1000, true, &index) ==
        VG_ERR_CAPACITY);

  CHECK(vg_core_transition(&core, 0, VG_TASK_SCHEDULED) == VG_OK);
  CHECK(vg_core_transition(&core, 0, VG_TASK_ALERTING) == VG_OK);
  CHECK(vg_core_transition(&core, 0, VG_TASK_SNOOZED) == VG_OK);
  CHECK(vg_core_transition(&core, 0, VG_TASK_SCHEDULED) == VG_OK);
  CHECK(vg_core_transition(&core, 0, VG_TASK_ALERTING) == VG_OK);
  CHECK(vg_core_transition(&core, 0, VG_TASK_ACKNOWLEDGED) == VG_OK);
  CHECK(vg_core_transition(&core, 0, VG_TASK_ALERTING) ==
        VG_ERR_INVALID_STATE);
  CHECK(vg_core_transition(&core, VG_MAX_ACTIVE_TASKS, VG_TASK_ALERTING) ==
        VG_ERR_TASK_NOT_FOUND);
}

static void test_protocol(void)
{
  const char *valid =
    "{\"version\":1,\"request_id\":\"demo-001\",\"type\":\"task.create\","
    "\"payload\":{\"title\":\"Submit VelaGuard\",\"due_epoch\":1787652000,"
    "\"priority\":1}}\n";
  const char *bad_version =
    "{\"version\":2,\"request_id\":\"x\",\"type\":\"task.create\","
    "\"payload\":{\"title\":\"x\",\"due_epoch\":2,\"priority\":1}}";
  const char *missing_title =
    "{\"version\":1,\"request_id\":\"x\",\"type\":\"task.create\","
    "\"payload\":{\"due_epoch\":2,\"priority\":1}}";
  const char *missing_version =
    "{\"request_id\":\"x\",\"type\":\"task.create\","
    "\"payload\":{\"title\":\"x\",\"due_epoch\":2,\"priority\":1}}";
  const char *trailing =
    "{\"version\":1,\"request_id\":\"x\",\"type\":\"task.create\","
    "\"payload\":{\"title\":\"x\",\"due_epoch\":2,\"priority\":1}}x";
  vg_create_request_t parsed;

  CHECK(vg_protocol_parse_create(valid, strlen(valid), &parsed) == VG_OK);
  CHECK(strcmp(parsed.request_id, "demo-001") == 0);
  CHECK(strcmp(parsed.title, "Submit VelaGuard") == 0);
  CHECK(parsed.due_epoch == INT64_C(1787652000));
  CHECK(parsed.priority == 1);
  CHECK(vg_protocol_parse_create("{", 1, &parsed) == VG_ERR_INVALID_JSON);
  CHECK(vg_protocol_parse_create(bad_version, strlen(bad_version), &parsed) ==
        VG_ERR_UNSUPPORTED_VERSION);
  CHECK(vg_protocol_parse_create(missing_title, strlen(missing_title), &parsed) ==
        VG_ERR_INVALID_MESSAGE);
  CHECK(vg_protocol_parse_create(missing_version, strlen(missing_version),
                                 &parsed) == VG_ERR_INVALID_MESSAGE);
  CHECK(vg_protocol_parse_create(trailing, strlen(trailing), &parsed) ==
        VG_ERR_INVALID_JSON);
}

static void test_protocol_bounds(void)
{
  char title[VG_TITLE_MAX_BYTES + 2];
  char line[VG_PROTOCOL_MAX_LINE_BYTES + 2];
  vg_create_request_t parsed;
  int length;

  memset(title, 'a', VG_TITLE_MAX_BYTES);
  title[VG_TITLE_MAX_BYTES] = '\0';
  length = snprintf(line, sizeof(line),
    "{\"version\":1,\"request_id\":\"bounds\",\"type\":\"task.create\","
    "\"payload\":{\"title\":\"%s\",\"due_epoch\":2000,\"priority\":0}}", title);
  CHECK(vg_protocol_parse_create(line, (size_t)length, &parsed) == VG_OK);
  title[VG_TITLE_MAX_BYTES] = 'a';
  title[VG_TITLE_MAX_BYTES + 1] = '\0';
  length = snprintf(line, sizeof(line),
    "{\"version\":1,\"request_id\":\"bounds\",\"type\":\"task.create\","
    "\"payload\":{\"title\":\"%s\",\"due_epoch\":2000,\"priority\":0}}", title);
  CHECK(vg_protocol_parse_create(line, (size_t)length, &parsed) ==
        VG_ERR_INVALID_MESSAGE);
  CHECK(vg_protocol_parse_create(NULL, 1, &parsed) == VG_ERR_INVALID_MESSAGE);
  CHECK(vg_protocol_parse_create(line, 0, &parsed) == VG_ERR_INVALID_MESSAGE);
  CHECK(vg_protocol_parse_create(line, sizeof(line), &parsed) ==
        VG_ERR_INVALID_MESSAGE);
  CHECK(vg_protocol_parse_create(line, 1, NULL) == VG_ERR_INVALID_MESSAGE);
}

static void test_embedded_nul(void)
{
  const char *bad_id =
    "{\"version\":1,\"request_id\":\"a\\u0000b\",\"type\":\"task.create\","
    "\"payload\":{\"title\":\"x\",\"due_epoch\":2000,\"priority\":1}}";
  const char *bad_type =
    "{\"version\":1,\"request_id\":\"a\",\"type\":\"task.create\\u0000x\","
    "\"payload\":{\"title\":\"x\",\"due_epoch\":2000,\"priority\":1}}";
  const char *literal =
    "{\"version\":1,\"request_id\":\"a\",\"type\":\"task.create\","
    "\"payload\":{\"title\":\"x\\\\u0000y\",\"due_epoch\":2000,\"priority\":1}}";
  vg_create_request_t parsed;
  CHECK(vg_protocol_parse_create(bad_id, strlen(bad_id), &parsed) ==
        VG_ERR_INVALID_MESSAGE);
  CHECK(vg_protocol_parse_create(bad_type, strlen(bad_type), &parsed) ==
        VG_ERR_INVALID_MESSAGE);
  CHECK(vg_protocol_parse_create(literal, strlen(literal), &parsed) == VG_OK);
  CHECK(strcmp(parsed.title, "x\\u0000y") == 0);
}

int main(void)
{
  test_create_and_deduplicate();
  test_time_capacity_and_transitions();
  test_protocol();
  test_protocol_bounds();
  test_embedded_nul();

  if (g_failures != 0)
    {
      fprintf(stderr, "%d test checks failed\n", g_failures);
      return 1;
    }

  puts("all core/protocol checks passed");
  return 0;
}
