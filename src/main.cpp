#include "engine.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
namespace {
bool output(const std::string &bytes) {
  const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!handle || handle == INVALID_HANDLE_VALUE)
    return false;
  DWORD mode = 0;
  if (GetConsoleMode(handle, &mode)) {
    const auto text = ece::wide(bytes);
    size_t offset = 0;
    while (offset < text.size()) {
      size_t count = (std::min)(size_t{16384}, text.size() - offset);
      const wchar_t last = text[offset + count - 1];
      if (last >= 0xD800 && last <= 0xDBFF && offset + count < text.size())
        --count;
      DWORD written = 0;
      if (!WriteConsoleW(handle, text.data() + offset,
                         static_cast<DWORD>(count), &written, nullptr) ||
          !written)
        return false;
      offset += written;
    }
  } else {
    size_t offset = 0;
    while (offset < bytes.size()) {
      const auto count =
          static_cast<DWORD>((std::min)(size_t{65536}, bytes.size() - offset));
      DWORD written = 0;
      if (!WriteFile(handle, bytes.data() + offset, count, &written, nullptr) ||
          !written)
        return false;
      offset += written;
    }
  }
  return true;
}
constexpr const char *help =
    "event-correlation-engine 1.0.0\nUsage: event-correlation-engine [--json] "
    "recent|groups|around [options]\n  --channel NAME       Repeat for local "
    "channels (default System, Application)\n  --record-id ID       Required "
    "for around; exactly one --channel is required\n  --minutes N          "
    "Recent lookback / around radius (default 60; 1..10080)\n  --max-events N  "
    "     Per-channel limit (default 1000; 1..10000)\n  --gap-seconds N      "
    "Inclusive adjacent clustering gap (default 2; 0..300)\n  --level N        "
    "    Exact Windows level (0..5)\n  --detailed-groups    Include task and "
    "opcode in group keys\n  --json               UTF-8 JSON; can appear "
    "before or after mode\n  --help / --version\nMaximum 16 channels; channel "
    "names must be local, without wildcard or path syntax.\nTemporal "
    "correlation does not establish causation. Local, read-only, no "
    "telemetry.\n";
std::uint64_t number(const std::wstring &s, std::uint64_t low,
                     std::uint64_t high) {
  if (s.empty() || !std::all_of(s.begin(), s.end(), [](wchar_t c) {
        return c >= L'0' && c <= L'9';
      }))
    throw std::invalid_argument("expected decimal integer");
  size_t used = 0;
  auto n = std::stoull(s, &used);
  if (used != s.size() || n < low || n > high)
    throw std::invalid_argument("integer outside permitted range");
  return n;
}
} // namespace
int wmain(int argc, wchar_t **argv) {
  try {
    ece::Options o;
    bool mode = false, channels = false;
    for (int i = 1; i < argc; ++i) {
      const std::wstring arg = argv[i];
      auto value = [&]() {
        if (i + 1 >= argc)
          throw std::invalid_argument("missing option value");
        return std::wstring(argv[++i]);
      };
      if (arg == L"--help") {
        return output(help) ? 0 : 6;
      }
      if (arg == L"--version") {
        return output("event-correlation-engine 1.0.0\n") ? 0 : 6;
      }
      if (arg == L"recent" || arg == L"groups" || arg == L"around") {
        if (mode)
          throw std::invalid_argument("only one mode allowed");
        o.mode = ece::utf8(arg);
        mode = true;
      } else if (arg == L"--json")
        o.json = true;
      else if (arg == L"--detailed-groups")
        o.detailed = true;
      else if (arg == L"--minutes")
        o.minutes = static_cast<unsigned>(number(value(), 1, 10080));
      else if (arg == L"--max-events")
        o.maxEvents = static_cast<unsigned>(number(value(), 1, 10000));
      else if (arg == L"--gap-seconds")
        o.gap = static_cast<unsigned>(number(value(), 0, 300));
      else if (arg == L"--level")
        o.level = static_cast<unsigned>(number(value(), 0, 5));
      else if (arg == L"--record-id")
        o.record = number(value(), 1, INT64_MAX);
      else if (arg == L"--channel") {
        auto c = value();
        if (c.empty() || c.size() > 256 ||
            c.find_first_of(L"\\:*?[]'\"<>|") != std::wstring::npos ||
            std::any_of(c.begin(), c.end(), [](wchar_t ch) { return ch < 32; }))
          throw std::invalid_argument("invalid local channel name");
        if (!channels) {
          o.channels.clear();
          channels = true;
        }
        if (std::find(o.channels.begin(), o.channels.end(), c) !=
            o.channels.end())
          throw std::invalid_argument("duplicate channel");
        o.channels.push_back(c);
        if (o.channels.size() > 16)
          throw std::invalid_argument("too many channels");
      } else
        throw std::invalid_argument("unknown argument");
    }
    if (!mode)
      throw std::invalid_argument("specify recent, groups or around");
    if (o.mode == "around") {
      if (!o.record || !channels || o.channels.size() != 1)
        throw std::invalid_argument(
            "around requires one channel and record ID");
      o.anchorChannel = o.channels.front();
      o.channels = {L"System", L"Application"};
      if (o.anchorChannel != L"System" && o.anchorChannel != L"Application")
        o.channels.push_back(o.anchorChannel);
    } else if (o.record)
      throw std::invalid_argument("record ID requires around mode");
    auto r = ece::acquire(o);
    return output(o.json ? ece::json(r) : ece::text(r)) ? r.exitCode : 6;
  } catch (const std::invalid_argument &e) {
    std::fprintf(stderr, "Usage error: %s\n%s", e.what(), help);
    return 2;
  } catch (const std::out_of_range &) {
    std::fprintf(stderr, "Usage error: integer outside permitted range\n");
    return 2;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "Internal/output error: %s\n", e.what());
    return 6;
  } catch (...) {
    std::fprintf(stderr, "Internal/output error\n");
    return 6;
  }
}
