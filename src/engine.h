#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>
namespace ece {
using Fields = std::map<std::string, std::string>;
struct Event {
  Event() = default;
  Event(const Event &) = default;
  Event &operator=(const Event &) = default;
  // MSVC std::map move construction allocates a sentinel and can throw.
  Event(Event &&) noexcept(false) = default;
  Event &operator=(Event &&) noexcept(false) = default;
  Fields fields;
  std::vector<Fields> data;
  std::uint64_t ticks = 0;
  std::optional<std::string> message;
  DWORD messageError = 0;
  std::string xml;
};
struct Channel {
  std::string name, status = "complete";
  DWORD error = 0;
  size_t retained = 0;
  bool truncated = false;
  std::string phase = "window";
};
struct Options {
  std::string mode = "recent";
  std::vector<std::wstring> channels{L"System", L"Application"};
  std::wstring anchorChannel;
  std::uint64_t record = 0;
  unsigned minutes = 60, maxEvents = 1000, gap = 2;
  std::optional<unsigned> level;
  bool json = false, detailed = false;
};
struct Report {
  Report() = default;
  Report(const Report &) = default;
  Report &operator=(const Report &) = default;
  Report(Report &&) noexcept(false) = default;
  Report &operator=(Report &&) noexcept(false) = default;
  Options options;
  std::uint64_t start = 0, end = 0;
  std::vector<Channel> channels;
  std::vector<Event> events;
  std::optional<Event> selected;
  int exitCode = 0;
  size_t retainedBytes = 0;
};
std::string utf8(const std::wstring &s);
std::wstring wide(const std::string &s);
std::string quote(const std::string &s);
std::string timestamp(std::uint64_t ticks);
std::uint64_t parseTime(const std::string &s);
Event parseXml(const std::wstring &xml);
void sortEvents(std::vector<Event> &events);
std::vector<std::vector<size_t>> clusters(const std::vector<Event> &events,
                                          unsigned gap);
std::map<std::string, std::vector<size_t>>
groups(const std::vector<Event> &events, bool detailed);
std::map<std::string, std::vector<size_t>>
evidence(const std::vector<Event> &events);
std::map<std::string, std::vector<size_t>>
selectedEvidence(const Report &report);
std::map<std::string, std::vector<size_t>>
recurringPatterns(const std::vector<Event> &events, unsigned gap,
                  bool detailed);
Report acquire(const Options &options);
std::string json(const Report &report);
std::string text(const Report &report);
constexpr const char *limitation =
    "Temporal correlation and shared identifiers do not establish causation. "
    "Missing records, retention, permissions, clock differences, and "
    "acquisition limits can hide evidence. Process IDs can be reused; "
    "identifiers are scoped to their computer/provider. No root cause is "
    "inferred.";
} // namespace ece
