#include "velaguard_protocol.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#ifdef __NuttX__
#  include <netutils/cJSON.h>
#else
#  include "cJSON.h"
#endif

static bool vg_json_integer(const cJSON *item)
{
  return cJSON_IsNumber(item) &&
         item->valuedouble == (double)item->valueint;
}

/* cJSON 的 C 字符串不保留嵌入 NUL 的长度，解码前拒绝，避免静默截断。
 * 跳过成对转义，保留合法的字面量反斜杠加 u0000。
 */
static bool vg_contains_nul(const char *line, size_t length)
{
  size_t i;
  for (i = 0; i < length; i++)
    {
      if (line[i] == '\0')
        {
          return true;
        }

      if (line[i] == '\\' && i + 1 < length)
        {
          if (length - i >= 6 && memcmp(line + i + 1, "u0000", 5) == 0)
            {
              return true;
            }

          i++;
        }
    }

  return false;
}

static bool vg_copy_string(char *destination, size_t capacity,
                           const cJSON *item)
{
  size_t length;

  if (!cJSON_IsString(item) || item->valuestring == NULL)
    {
      return false;
    }

  length = strlen(item->valuestring);
  if (length == 0 || length >= capacity)
    {
      return false;
    }

  memcpy(destination, item->valuestring, length + 1);
  return true;
}

vg_result_t vg_protocol_parse_create(const char *line, size_t length,
                                     vg_create_request_t *out)
{
  const char *parse_end;
  const char *limit;
  const cJSON *version;
  const cJSON *request_id;
  const cJSON *type;
  const cJSON *payload;
  const cJSON *title;
  const cJSON *due_epoch;
  const cJSON *priority;
  cJSON *root;
  vg_result_t result = VG_ERR_INVALID_MESSAGE;

  if (line == NULL || out == NULL || length == 0 ||
      length > VG_PROTOCOL_MAX_LINE_BYTES)
    {
      return VG_ERR_INVALID_MESSAGE;
    }

  if (vg_contains_nul(line, length))
    {
      return VG_ERR_INVALID_MESSAGE;
    }

  parse_end = NULL;
  root = cJSON_ParseWithLengthOpts(line, length, &parse_end, false);
  if (root == NULL || parse_end == NULL)
    {
      cJSON_Delete(root);
      return VG_ERR_INVALID_JSON;
    }

  limit = line + length;
  while (parse_end < limit && isspace((unsigned char)*parse_end))
    {
      parse_end++;
    }

  if (parse_end != limit || !cJSON_IsObject(root))
    {
      result = VG_ERR_INVALID_JSON;
      goto out;
    }

  version = cJSON_GetObjectItemCaseSensitive(root, "version");
  if (version == NULL)
    {
      goto out;
    }

  if (!vg_json_integer(version) || version->valueint != VG_PROTOCOL_VERSION)
    {
      result = VG_ERR_UNSUPPORTED_VERSION;
      goto out;
    }

  request_id = cJSON_GetObjectItemCaseSensitive(root, "request_id");
  type = cJSON_GetObjectItemCaseSensitive(root, "type");
  payload = cJSON_GetObjectItemCaseSensitive(root, "payload");
  if (!cJSON_IsString(type) || type->valuestring == NULL ||
      strcmp(type->valuestring, "task.create") != 0 ||
      !cJSON_IsObject(payload))
    {
      goto out;
    }

  title = cJSON_GetObjectItemCaseSensitive(payload, "title");
  due_epoch = cJSON_GetObjectItemCaseSensitive(payload, "due_epoch");
  priority = cJSON_GetObjectItemCaseSensitive(payload, "priority");

  memset(out, 0, sizeof(*out));
  if (!vg_copy_string(out->request_id, sizeof(out->request_id), request_id) ||
      !vg_copy_string(out->title, sizeof(out->title), title) ||
      !vg_json_integer(due_epoch) || due_epoch->valueint <= 0 ||
      !vg_json_integer(priority) || priority->valueint < 0 ||
      priority->valueint > 2)
    {
      goto out;
    }

  out->due_epoch = (int64_t)due_epoch->valueint;
  out->priority = (int)priority->valueint;
  result = VG_OK;

out:
  cJSON_Delete(root);
  return result;
}
