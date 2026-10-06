#include "engine.h"
#include <cstdio>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value, const char *name) {
  if (!value)
    throw std::runtime_error(name);
}
ece::Event
fixture(const wchar_t *time = L"2026-01-01T00:00:00.0000000Z",
        const wchar_t *activity = L"{11111111-1111-1111-1111-111111111111}") {
  return ece::parseXml(
      std::wstring(
          L"<Event "
          L"xmlns='http://schemas.microsoft.com/win/2004/08/events/"
          L"event'><System><Provider "
          L"Name='Fixture'/><EventID>7</EventID><Version>1</Version><Level>4</"
          L"Level><Task>0</Task><Opcode>0</Opcode><Keywords>0x0</"
          L"Keywords><TimeCreated SystemTime='") +
      time + L"'/><EventRecordID>2</EventRecordID><Correlation ActivityID='" +
      activity +
      L"'/><Execution ProcessID='123' "
      L"ThreadID='8'/><Channel>System</Channel><Computer>host</Computer></"
      L"System><EventData><Data "
      L"Name='value'>&lt;test&gt;&amp;</Data></EventData></Event>");
}
} // namespace
int main(int argc, char **argv) {
  try {
    auto a = fixture();
    check(!a.message, "missing message");
    check(a.data.back().at("value") == "<test>&", "XML entities");
    check(ece::timestamp(ece::parseTime("2026-01-01T00:00:00.1234567Z")) ==
              "2026-01-01T00:00:00.1234567Z",
          "timestamp precision");
    check(ece::timestamp(ece::parseTime("2024-02-29T23:59:59.1Z")) ==
              "2024-02-29T23:59:59.1000000Z",
          "leap day");
    auto tied = std::vector<ece::Event>{a, a};
    tied[0].fields["record_id"] = "10";
    tied[1].fields["record_id"] = "2";
    ece::sortEvents(tied);
    check(tied[0].fields.at("record_id") == "2", "numeric record ordering");
    std::vector<ece::Event> events{fixture(L"2026-01-01T00:00:04.0000001Z"),
                                   fixture(L"2026-01-01T00:00:02.0000000Z"), a};
    ece::sortEvents(events);
    for (size_t i = 0; i < events.size(); ++i)
      events[i].fields["record_id"] = std::to_string(i + 1);
    check(events.front().ticks == a.ticks, "ordering");
    auto c = ece::clusters(events, 2);
    check(c.size() == 2 && c[0].size() == 2, "inclusive cluster boundary");
    check(ece::groups(events, false).begin()->second.size() == 3,
          "group counts");
    check(ece::evidence(events).begin()->second.size() == 3, "shared activity");
    check(ece::recurringPatterns(events, 2, false).begin()->second.size() == 2,
          "recurring group sets across windows");
    auto absent = fixture(L"2026-01-01T00:00:00Z", L"");
    check(absent.fields.count("activity_id") == 0, "missing activity");
    for (const auto &p : ece::evidence({absent}))
      check(p.first.find("activity_id") == std::string::npos,
            "absent evidence");
    ece::Report r;
    r.start = a.ticks;
    r.end = events.back().ticks;
    r.events = events;
    r.selected = events[1];
    check(ece::selectedEvidence(r).begin()->second.size() == 2,
          "selected shared evidence");
    auto scoped = events;
    scoped[2].fields["computer"] = "another-host";
    check(ece::evidence(scoped).begin()->second.size() != 3, "computer scope");
    r.channels.push_back({"System", "complete", 0, 3, false});
    r.channels.push_back({"Security", "access_denied", 5, 0, false});
    auto j = ece::json(r);
    check(j.find("\"status\":\"partial\"") != std::string::npos,
          "partial acquisition");
    check(j.find("\"preceding\":[0]") != std::string::npos &&
              j.find("\"following\":[2]") != std::string::npos,
          "selected window sides");
    check(j.find("do not establish causation") != std::string::npos,
          "no causality");
    check(j.find("\"message\":null") != std::string::npos, "null message");
    r.channels[0].truncated = true;
    check(ece::json(r).find("event limit reached") != std::string::npos,
          "bounded limit warning");
    check(ece::clusters({}, 2).empty() && ece::groups({}, false).empty(),
          "empty results");
    ece::Report empty;
    empty.start = empty.end = a.ticks;
    empty.channels.push_back({"System", "complete", 0, 0, false});
    check(ece::json(empty).find("\"events\":[]") != std::string::npos,
          "empty JSON");
    auto zero = absent;
    zero.fields["activity_id"] = "{00000000-0000-0000-0000-000000000000}";
    for (const auto &p : ece::evidence({zero}))
      check(p.first.find("activity_id") == std::string::npos,
            "zero activity excluded");
    auto related = absent;
    related.fields["related_activity_id"] =
        "{11111111-1111-1111-1111-111111111111}";
    check(ece::evidence({a, related}).begin()->second.size() == 2,
          "related activity links");
    for (const auto *xml : {L"<Event>",
                            L"<!DOCTYPE Event [<!ENTITY x SYSTEM "
                            L"'file:///test'>]><Event>&x;</Event>",
                            L"<Event><System/></Event>"}) {
      bool failed = false;
      try {
        ece::parseXml(xml);
      } catch (const std::exception &) {
        failed = true;
      }
      check(failed, "malformed/DTD XML");
    }
    check(ece::quote("\n\"\\") == "\"\\u000a\\\"\\\\\"", "JSON escapes");
    if (argc == 2 && std::string(argv[1]) == "--json")
      std::cout << ece::json(r);
    else
      std::cout << "All engine tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  } catch (...) {
    return 1;
  }
}
