#include "../../hairclip/json_writer.h"
#include "test.h"

TEST(json_writer_builds_nested_documents) {
  char buf[256];
  text::StrBuf b(buf, sizeof(buf));
  JsonWriter j(b);
  j.beginObject().num("a", -5).str("b", "x\"y").boolean("ok", true);
  j.beginObject("c").unum("d", 4294967295u).endObject();
  j.beginArray("e").num(nullptr, 1).str(nullptr, "ș").beginObject().endObject().endArray();
  j.endObject();
  CHECK(j.ok());
  CHECK_STR(b.c_str(), "{\"a\":-5,\"b\":\"x\\\"y\",\"ok\":true,\"c\":{\"d\":4294967295},\"e\":[1,\"ș\",{}]}");
}

TEST(json_writer_escapes_keys) {
  char buf[64];
  text::StrBuf b(buf, sizeof(buf));
  JsonWriter j(b);
  j.beginObject().str("k\"<", "v").endObject();
  CHECK_STR(b.c_str(), "{\"k\\\"\\u003c\":\"v\"}");
}

TEST(json_writer_reports_overflow) {
  char buf[16];
  text::StrBuf b(buf, sizeof(buf));
  JsonWriter j(b);
  j.beginObject().str("message", "far too long for this buffer").endObject();
  CHECK(!j.ok());
}

TEST(json_writer_reports_unbalanced_use) {
  char buf[64];
  text::StrBuf b(buf, sizeof(buf));
  JsonWriter open(b);
  open.beginObject();
  CHECK(!open.ok());
  b.clear();
  JsonWriter extra(b);
  extra.endObject();
  CHECK(!extra.ok());
}
