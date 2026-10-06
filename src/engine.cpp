#ifdef _MSC_VER
// SDK XmlLite SetProperty has an invalid SAL pointer annotation on ULONG_PTR.
#pragma warning(push)
#pragma warning(disable : 6553)
#endif
#include "engine.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <memory>
#include <objbase.h>
#include <shlwapi.h>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <winevt.h>
#include <xmllite.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
namespace ece {
namespace {
struct ApiFailure : std::runtime_error {
  DWORD code;
  ApiFailure(const char *message, DWORD error)
      : std::runtime_error(message), code(error) {}
};
struct Handle {
  EVT_HANDLE h;
  explicit Handle(EVT_HANDLE v) : h(v) {}
  ~Handle() {
    if (h)
      EvtClose(h);
  }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
};
template <class T> struct Release {
  void operator()(T *p) const {
    if (p)
      p->Release();
  }
};
std::string field(const Event &e, const std::string &k) {
  auto i = e.fields.find(k);
  return i == e.fields.end() ? "" : i->second;
}
std::string object(const Fields &f) {
  std::string s = "{";
  for (const auto &p : f) {
    if (s.size() > 1)
      s += ",";
    s += quote(p.first) + ":" + quote(p.second);
  }
  return s + "}";
}
std::string eventJson(const Event &e) {
  std::string fields = "{";
  for (const auto *key :
       {"channel", "provider", "event_id", "version", "level", "task", "opcode",
        "keywords", "record_id", "timestamp", "process_id", "thread_id",
        "activity_id", "related_activity_id", "user_sid", "computer"}) {
    if (fields.size() > 1)
      fields += ",";
    const auto i = e.fields.find(key);
    fields +=
        quote(key) + ":" + (i == e.fields.end() ? "null" : quote(i->second));
  }
  fields += "}";
  std::string s =
      "{\"fields\":" + fields + ",\"xml\":" + quote(e.xml) +
      ",\"message\":" + (e.message ? quote(*e.message) : "null") +
      ",\"message_status\":" + quote(e.message ? "available" : "unavailable") +
      ",\"message_error\":" + std::to_string(e.messageError) + ",\"data\":[";
  for (size_t i = 0; i < e.data.size(); ++i) {
    if (i)
      s += ",";
    s += object(e.data[i]);
  }
  return s + "]}";
}
DWORD classify(DWORD err) {
  return err == ERROR_ACCESS_DENIED ? 4U
                                    : (err == ERROR_EVT_CHANNEL_NOT_FOUND ||
                                               err == ERROR_FILE_NOT_FOUND
                                           ? 3U
                                           : 5U);
}
Event render(EVT_HANDLE h) {
  DWORD used = 0, count = 0;
  EvtRender(nullptr, h, EvtRenderEventXml, 0, nullptr, &used, &count);
  const DWORD sizeError = GetLastError();
  if (sizeError != ERROR_INSUFFICIENT_BUFFER)
    throw ApiFailure("render size", sizeError);
  if (used > 4U * 1024U * 1024U)
    throw ApiFailure("render resource limit", ERROR_NOT_ENOUGH_MEMORY);
  std::vector<wchar_t> buffer(used / sizeof(wchar_t) + 1);
  if (!EvtRender(nullptr, h, EvtRenderEventXml, used, buffer.data(), &used,
                 &count))
    throw ApiFailure("render failed", GetLastError());
  Event e = parseXml(buffer.data());
  const auto provider = wide(field(e, "provider"));
  Handle metadata(
      EvtOpenPublisherMetadata(nullptr, provider.c_str(), nullptr, 0, 0));
  if (!metadata.h) {
    e.messageError = GetLastError();
    return e;
  }
  DWORD chars = 0;
  EvtFormatMessage(metadata.h, h, 0, 0, nullptr, EvtFormatMessageEvent, 0,
                   nullptr, &chars);
  e.messageError = GetLastError();
  if (e.messageError == ERROR_INSUFFICIENT_BUFFER && chars <= 1024 * 1024) {
    std::vector<wchar_t> msg(chars);
    if (EvtFormatMessage(metadata.h, h, 0, 0, nullptr, EvtFormatMessageEvent,
                         chars, msg.data(), &chars)) {
      e.message = utf8(msg.data());
      e.messageError = 0;
    } else
      e.messageError = GetLastError();
  }
  return e;
}
void query(Report &r, const std::wstring &channel, const std::wstring &xpath,
           size_t limit, bool anchor) {
  Channel c;
  c.name = utf8(channel);
  c.phase = anchor ? "anchor" : "window";
  Handle config(EvtOpenChannelConfig(nullptr, channel.c_str(), 0));
  if (!config.h) {
    c.error = GetLastError();
    c.status = c.error == ERROR_ACCESS_DENIED ? "access_denied" : "unavailable";
    r.channels.push_back(c);
    return;
  }
  EVT_VARIANT enabled{};
  DWORD used = 0;
  if (!EvtGetChannelConfigProperty(config.h, EvtChannelConfigEnabled, 0,
                                   sizeof(enabled), &enabled, &used)) {
    c.error = GetLastError();
    c.status = "api_failure";
    r.channels.push_back(c);
    return;
  }
  if (enabled.Type != EvtVarTypeBoolean || !enabled.BooleanVal) {
    c.error = ERROR_EVT_CHANNEL_NOT_FOUND;
    c.status = "disabled";
    r.channels.push_back(c);
    return;
  }
  Handle q(EvtQuery(nullptr, channel.c_str(), xpath.c_str(),
                    static_cast<DWORD>(EvtQueryChannelPath) |
                        static_cast<DWORD>(EvtQueryReverseDirection)));
  if (!q.h) {
    c.error = GetLastError();
    c.status = c.error == ERROR_ACCESS_DENIED ? "access_denied" : "unavailable";
    r.channels.push_back(c);
    return;
  }
  for (;;) {
    EVT_HANDLE raw = nullptr;
    DWORD returned = 0;
    if (!EvtNext(q.h, 1, &raw, 5000, 0, &returned)) {
      c.error = GetLastError();
      if (c.error == ERROR_NO_MORE_ITEMS)
        c.error = 0;
      else
        c.status = "api_failure";
      break;
    }
    Handle ev(raw);
    if (c.retained >= limit) {
      c.truncated = true;
      break;
    }
    try {
      Event e = render(ev.h);
      size_t bytes = e.xml.size() + (e.message ? e.message->size() : 0);
      for (const auto &p : e.fields)
        bytes += p.first.size() + p.second.size();
      for (const auto &d : e.data)
        for (const auto &p : d)
          bytes += p.first.size() + p.second.size();
      if (bytes > size_t{64} * 1024 * 1024 - r.retainedBytes) {
        c.status = "data_limit";
        c.truncated = true;
        break;
      }
      r.retainedBytes += bytes;
      if (anchor)
        r.selected = e;
      else
        r.events.push_back(std::move(e));
      ++c.retained;
    } catch (const std::bad_alloc &) {
      throw;
    } catch (const ApiFailure &failure) {
      c.error = failure.code;
      c.status = "api_failure";
      break;
    } catch (const std::exception &) {
      c.error = ERROR_EVT_INVALID_EVENT_DATA;
      c.status = "invalid_event";
      break;
    }
    if (anchor)
      break;
  }
  if (anchor && !c.error && !c.retained)
    c.status = "event_not_found";
  r.channels.push_back(c);
}
} // namespace
std::string utf8(const std::wstring &s) {
  if (s.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(),
                              static_cast<int>(s.size()), nullptr, 0, nullptr,
                              nullptr);
  if (!n)
    throw std::runtime_error("invalid Unicode");
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(),
                      static_cast<int>(s.size()), out.data(), n, nullptr,
                      nullptr);
  return out;
}
std::wstring wide(const std::string &s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                              static_cast<int>(s.size()), nullptr, 0);
  if (!n)
    throw std::runtime_error("invalid UTF-8");
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                      static_cast<int>(s.size()), out.data(), n);
  return out;
}
std::string quote(const std::string &s) {
  std::string out = "\"";
  const char *hex = "0123456789abcdef";
  for (unsigned char ch : s) {
    if (ch == '"' || ch == '\\') {
      out += '\\';
      out += static_cast<char>(ch);
    } else if (ch < 32) {
      out += "\\u00";
      out += hex[ch >> 4U];
      out += hex[ch & 15U];
    } else
      out += static_cast<char>(ch);
  }
  return out + '"';
}
std::string timestamp(std::uint64_t ticks) {
  FILETIME ft{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32U)};
  SYSTEMTIME st{};
  if (!FileTimeToSystemTime(&ft, &st))
    throw std::runtime_error("time out of range");
  std::ostringstream s;
  s << std::setfill('0') << std::setw(4) << st.wYear << '-' << std::setw(2)
    << st.wMonth << '-' << std::setw(2) << st.wDay << 'T' << std::setw(2)
    << st.wHour << ':' << std::setw(2) << st.wMinute << ':' << std::setw(2)
    << st.wSecond << '.' << std::setw(7) << (ticks % 10000000) << 'Z';
  return s.str();
}
std::uint64_t parseTime(const std::string &s) {
  if (s.size() < 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' ||
      s[13] != ':' || s[16] != ':' || s.back() != 'Z')
    throw std::runtime_error("invalid timestamp");
  auto num = [&](size_t pos, size_t len) {
    unsigned v = 0;
    for (size_t i = pos; i < pos + len; ++i) {
      if (s[i] < '0' || s[i] > '9')
        throw std::runtime_error("invalid timestamp digit");
      v = v * 10 + static_cast<unsigned>(s[i] - '0');
    }
    return static_cast<WORD>(v);
  };
  SYSTEMTIME st{};
  st.wYear = num(0, 4);
  st.wMonth = num(5, 2);
  st.wDay = num(8, 2);
  st.wHour = num(11, 2);
  st.wMinute = num(14, 2);
  st.wSecond = num(17, 2);
  FILETIME ft{};
  if (!SystemTimeToFileTime(&st, &ft))
    throw std::runtime_error("invalid date");
  std::uint64_t fraction = 0;
  if (s.size() > 20) {
    if (s[19] != '.' || s.size() > 28 || s.size() < 22)
      throw std::runtime_error("invalid fraction");
    size_t digits = s.size() - 21;
    for (size_t i = 20; i < s.size() - 1; ++i) {
      if (s[i] < '0' || s[i] > '9')
        throw std::runtime_error("invalid fraction");
      fraction = fraction * 10 + static_cast<unsigned>(s[i] - '0');
    }
    for (size_t i = digits; i < 7; ++i)
      fraction *= 10;
  }
  return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32U |
          ft.dwLowDateTime) +
         fraction;
}
Event parseXml(const std::wstring &xml) {
  if (xml.size() > size_t{2} * 1024 * 1024)
    throw std::runtime_error("XML too large");
  const std::string bytes = utf8(xml);
  std::unique_ptr<IStream, Release<IStream>> stream(
      SHCreateMemStream(reinterpret_cast<const BYTE *>(bytes.data()),
                        static_cast<UINT>(bytes.size())));
  IXmlReader *raw = nullptr;
  if (FAILED(CreateXmlReader(__uuidof(IXmlReader),
                             reinterpret_cast<void **>(&raw), nullptr)))
    throw std::runtime_error("XML reader");
  std::unique_ptr<IXmlReader, Release<IXmlReader>> reader(raw);
  if (!stream ||
      FAILED(reader->SetProperty(XmlReaderProperty_DtdProcessing,
                                 DtdProcessing_Prohibit)) ||
      FAILED(reader->SetProperty(XmlReaderProperty_MaxElementDepth, 64)) ||
      FAILED(reader->SetInput(stream.get())))
    throw std::runtime_error("XML input");
  Event e;
  e.xml = bytes;
  std::vector<std::string> path;
  std::string current;
  Fields attrs;
  bool root = false;
  XmlNodeType type{};
  HRESULT hr = S_OK;
  while ((hr = reader->Read(&type)) == S_OK) {
    const wchar_t *value = nullptr;
    if (type == XmlNodeType_Element) {
      reader->GetLocalName(&value, nullptr);
      current = utf8(value);
      if (path.empty()) {
        if (root || current != "Event")
          throw std::runtime_error("invalid root");
        root = true;
      }
      path.push_back(current);
      attrs.clear();
      if (reader->MoveToFirstAttribute() == S_OK) {
        do {
          const wchar_t *name = nullptr;
          reader->GetLocalName(&name, nullptr);
          reader->GetValue(&value, nullptr);
          attrs[utf8(name)] = utf8(value);
          if (attrs.size() > 128)
            throw std::runtime_error("attribute limit");
        } while (reader->MoveToNextAttribute() == S_OK);
        reader->MoveToElement();
      }
      if (path.size() == 3 && path[1] == "System") {
        auto copy = [&](const char *a, const char *b) {
          auto i = attrs.find(a);
          if (i != attrs.end() && !i->second.empty())
            e.fields[b] = i->second;
        };
        if (current == "Provider")
          copy("Name", "provider");
        if (current == "TimeCreated")
          copy("SystemTime", "timestamp");
        if (current == "Execution") {
          copy("ProcessID", "process_id");
          copy("ThreadID", "thread_id");
        }
        if (current == "Correlation") {
          copy("ActivityID", "activity_id");
          copy("RelatedActivityID", "related_activity_id");
        }
        if (current == "Security")
          copy("UserID", "user_sid");
      }
      if (path.size() >= 3 && path[1] != "System") {
        Fields d = attrs;
        d["element"] = current;
        std::string location;
        for (const auto &part : path)
          location += "/" + part;
        d["path"] = location;
        e.data.push_back(std::move(d));
        if (e.data.size() > 16384)
          throw std::runtime_error("data element limit");
      }
      if (reader->IsEmptyElement())
        path.pop_back();
    } else if (type == XmlNodeType_Text || type == XmlNodeType_CDATA ||
               type == XmlNodeType_Whitespace) {
      reader->GetValue(&value, nullptr);
      std::string v = utf8(value);
      if (path.size() == 3 && path[1] == "System") {
        static const std::map<std::string, std::string> keys{
            {"EventID", "event_id"},
            {"Version", "version"},
            {"Level", "level"},
            {"Task", "task"},
            {"Opcode", "opcode"},
            {"Keywords", "keywords"},
            {"EventRecordID", "record_id"},
            {"Channel", "channel"},
            {"Computer", "computer"}};
        auto i = keys.find(path.back());
        if (i != keys.end())
          e.fields[i->second] += v;
      } else if (path.size() >= 3 && !e.data.empty())
        e.data.back()["value"] += v;
    } else if (type == XmlNodeType_EndElement) {
      if (path.empty())
        throw std::runtime_error("XML nesting");
      path.pop_back();
    }
  }
  if (FAILED(hr) || !root || !path.empty() || field(e, "provider").empty() ||
      field(e, "event_id").empty() || field(e, "record_id").empty() ||
      field(e, "timestamp").empty())
    throw std::runtime_error("malformed event XML");
  e.ticks = parseTime(field(e, "timestamp"));
  e.fields["timestamp"] = timestamp(e.ticks);
  const std::string record = field(e, "record_id");
  if (record.empty() || !std::all_of(record.begin(), record.end(), [](char ch) {
        return ch >= '0' && ch <= '9';
      }))
    throw std::runtime_error("invalid record ID");
  size_t consumed = 0;
  (void)std::stoull(record, &consumed);
  if (consumed != record.size())
    throw std::runtime_error("invalid record ID");
  return e;
}
void sortEvents(std::vector<Event> &events) {
  std::sort(events.begin(), events.end(), [](const Event &a, const Event &b) {
    return std::make_tuple(a.ticks, field(a, "channel"),
                           std::stoull(field(a, "record_id")),
                           field(a, "provider")) <
           std::make_tuple(b.ticks, field(b, "channel"),
                           std::stoull(field(b, "record_id")),
                           field(b, "provider"));
  });
}
std::vector<std::vector<size_t>> clusters(const std::vector<Event> &events,
                                          unsigned gap) {
  std::vector<std::vector<size_t>> out;
  for (size_t i = 0; i < events.size(); ++i) {
    if (i == 0 || events[i].ticks - events[i - 1].ticks >
                      static_cast<std::uint64_t>(gap) * 10000000)
      out.emplace_back();
    out.back().push_back(i);
  }
  return out;
}
std::map<std::string, std::vector<size_t>>
groups(const std::vector<Event> &events, bool detailed) {
  std::map<std::string, std::vector<size_t>> out;
  for (size_t i = 0; i < events.size(); ++i) {
    std::string k = quote(field(events[i], "provider")) + ":" +
                    field(events[i], "event_id") + ":" +
                    field(events[i], "level");
    if (detailed)
      k += ":" + field(events[i], "task") + ":" + field(events[i], "opcode");
    out[k].push_back(i);
  }
  return out;
}
std::map<std::string, std::vector<size_t>>
evidence(const std::vector<Event> &events) {
  std::map<std::string, std::vector<size_t>> out;
  for (size_t i = 0; i < events.size(); ++i)
    for (const auto *k : {"activity_id", "related_activity_id", "process_id",
                          "thread_id", "user_sid", "computer"}) {
      std::string v = field(events[i], k);
      if (field(events[i], "computer").empty())
        continue;
      if (v.empty() || v == "0" ||
          v == "{00000000-0000-0000-0000-000000000000}")
        continue;
      const std::string kind =
          (std::string(k) == "related_activity_id" ? "activity_id" : k);
      if (kind == "activity_id")
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
          return static_cast<char>(std::tolower(c));
        });
      std::string key =
          kind + ":" + quote(field(events[i], "computer")) + ":" + quote(v);
      if (kind == "thread_id") {
        const auto process = field(events[i], "process_id");
        if (process.empty() || process == "0")
          continue;
        key += ":process:" + quote(process);
      }
      auto &indices = out[key];
      if (indices.empty() || indices.back() != i)
        indices.push_back(i);
    }
  return out;
}
std::map<std::string, std::vector<size_t>> selectedEvidence(const Report &r) {
  std::map<std::string, std::vector<size_t>> out;
  if (!r.selected)
    return out;
  const auto selected = evidence({*r.selected});
  const auto nearby = evidence(r.events);
  for (const auto &p : selected) {
    auto found = nearby.find(p.first);
    if (found == nearby.end())
      continue;
    for (auto i : found->second) {
      if (field(r.events[i], "channel") == field(*r.selected, "channel") &&
          field(r.events[i], "record_id") == field(*r.selected, "record_id"))
        continue;
      out[p.first].push_back(i);
    }
  }
  return out;
}
std::map<std::string, std::vector<size_t>>
recurringPatterns(const std::vector<Event> &events, unsigned gap,
                  bool detailed) {
  std::vector<std::string> keys(events.size());
  for (const auto &g : groups(events, detailed))
    for (auto i : g.second)
      keys[i] = g.first;
  std::map<std::string, std::vector<size_t>> out;
  const auto windows = clusters(events, gap);
  for (size_t n = 0; n < windows.size(); ++n) {
    std::map<std::string, bool> unique;
    for (auto i : windows[n])
      unique[keys[i]] = true;
    std::string signature = "[";
    for (const auto &p : unique) {
      if (signature.size() > 1)
        signature += ",";
      signature += quote(p.first);
    }
    signature += "]";
    out[signature].push_back(n);
  }
  return out;
}
Report acquire(const Options &o) {
  Report r;
  r.options = o;
  FILETIME now{};
  GetSystemTimeAsFileTime(&now);
  r.end = (static_cast<std::uint64_t>(now.dwHighDateTime) << 32U) |
          now.dwLowDateTime;
  const auto delta = static_cast<std::uint64_t>(o.minutes) * 60 * 10000000;
  r.start = r.end - delta;
  if (o.mode == "around") {
    query(r, o.anchorChannel,
          L"*[System[EventRecordID=" + std::to_wstring(o.record) + L"]]", 1,
          true);
    if (!r.selected) {
      r.exitCode = r.channels.back().error
                       ? static_cast<int>(classify(r.channels.back().error))
                       : 3;
      return r;
    }
    r.start = r.selected->ticks > delta ? r.selected->ticks - delta : 0;
    r.end = r.selected->ticks + delta;
  }
  const std::wstring filter =
      L"*[System[TimeCreated[@SystemTime>='" + wide(timestamp(r.start)) +
      L"' and @SystemTime<='" + wide(timestamp(r.end)) + L"']" +
      (o.level ? L" and Level=" + std::to_wstring(*o.level) : L"") + L"]]";
  for (const auto &c : o.channels)
    query(r, c, filter, o.maxEvents, false);
  sortEvents(r.events);
  bool success = false;
  for (const auto &c : r.channels) {
    if (c.phase == "anchor")
      continue;
    if (c.status == "complete" || c.retained > 0)
      success = true;
    else if (!r.exitCode)
      r.exitCode = static_cast<int>(classify(c.error));
  }
  if (success)
    r.exitCode = 0;
  return r;
}
std::string json(const Report &r) {
  const bool windowAvailable =
      r.options.mode != "around" || r.selected.has_value();
  std::string s =
      "{\"schema_version\":\"1.0\",\"version\":\"1.0.0\",\"query\":{\"mode\":" +
      quote(r.options.mode) +
      ",\"start\":" + (windowAvailable ? quote(timestamp(r.start)) : "null") +
      ",\"end\":" + (windowAvailable ? quote(timestamp(r.end)) : "null") +
      ",\"window_status\":" +
      quote(windowAvailable ? "available" : "anchor_unavailable") +
      ",\"minutes\":" + std::to_string(r.options.minutes) +
      ",\"max_events_per_channel\":" + std::to_string(r.options.maxEvents) +
      ",\"cluster_gap_seconds\":" + std::to_string(r.options.gap) +
      ",\"level\":" +
      (r.options.level ? std::to_string(*r.options.level) : "null") +
      ",\"detailed_groups\":" + (r.options.detailed ? "true" : "false") +
      ",\"anchor_channel\":" +
      (r.options.anchorChannel.empty() ? "null"
                                       : quote(utf8(r.options.anchorChannel))) +
      ",\"anchor_record_id\":" +
      (r.options.record ? quote(std::to_string(r.options.record)) : "null") +
      ",\"channels\":[";
  for (size_t i = 0; i < r.options.channels.size(); ++i) {
    if (i)
      s += ",";
    s += quote(utf8(r.options.channels[i]));
  }
  s += "]},\"acquisition\":{\"exit_code\":" + std::to_string(r.exitCode) +
       ",\"status\":";
  bool partial = false;
  for (const auto &c : r.channels)
    if (c.error || c.truncated)
      partial = true;
  s += quote(r.exitCode ? "failed" : (partial ? "partial" : "complete")) +
       ",\"events_retained\":" + std::to_string(r.events.size()) +
       "},\"channels\":[";
  for (size_t i = 0; i < r.channels.size(); ++i) {
    if (i)
      s += ",";
    const auto &c = r.channels[i];
    s += "{\"name\":" + quote(c.name) + ",\"status\":" + quote(c.status) +
         ",\"error\":" + std::to_string(c.error) +
         ",\"retained\":" + std::to_string(c.retained) +
         ",\"phase\":" + quote(c.phase) +
         ",\"truncated\":" + (c.truncated ? "true" : "false") + "}";
  }
  s +=
      "],\"selected_event\":" + (r.selected ? eventJson(*r.selected) : "null") +
      ",\"events\":[";
  for (size_t i = 0; i < r.events.size(); ++i) {
    if (i)
      s += ",";
    s += eventJson(r.events[i]);
  }
  s += "],\"groups\":[";
  bool first = true;
  for (const auto &g : groups(r.events, r.options.detailed)) {
    if (!first)
      s += ",";
    first = false;
    const auto &a = r.events[g.second.front()];
    const auto &b = r.events[g.second.back()];
    s += "{\"key\":" + quote(g.first) +
         ",\"count\":" + std::to_string(g.second.size()) +
         ",\"first\":" + quote(timestamp(a.ticks)) +
         ",\"last\":" + quote(timestamp(b.ticks)) +
         ",\"representative_message\":";
    std::optional<std::string> message;
    std::map<std::string, bool> channels;
    size_t metadata = 0;
    for (auto i : g.second) {
      if (!message && r.events[i].message)
        message = r.events[i].message;
      channels[field(r.events[i], "channel")] = true;
      if (!field(r.events[i], "activity_id").empty() ||
          !field(r.events[i], "process_id").empty())
        ++metadata;
    }
    s += (message ? quote(*message) : "null") +
         ",\"events_with_activity_or_process\":" + std::to_string(metadata) +
         ",\"channels\":[";
    bool cf = true;
    for (const auto &c : channels) {
      if (!cf)
        s += ",";
      cf = false;
      s += quote(c.first);
    }
    s += "]}";
  }
  s += "],\"temporal_clusters\":[";
  first = true;
  for (const auto &c : clusters(r.events, r.options.gap)) {
    if (!first)
      s += ",";
    first = false;
    s += "[";
    for (size_t j = 0; j < c.size(); ++j) {
      if (j)
        s += ",";
      s += std::to_string(c[j]);
    }
    s += "]";
  }
  s += "],\"cluster_patterns\":[";
  first = true;
  for (const auto &p :
       recurringPatterns(r.events, r.options.gap, r.options.detailed)) {
    if (!first)
      s += ",";
    first = false;
    s += "{\"group_keys\":" + p.first +
         ",\"window_count\":" + std::to_string(p.second.size()) +
         ",\"cluster_indices\":[";
    for (size_t j = 0; j < p.second.size(); ++j) {
      if (j)
        s += ",";
      s += std::to_string(p.second[j]);
    }
    s += "]}";
  }
  s += "],\"selected_shared_evidence\":[";
  first = true;
  for (const auto &p : selectedEvidence(r)) {
    if (!first)
      s += ",";
    first = false;
    s += "{\"identifier\":" + quote(p.first) + ",\"event_indices\":[";
    for (size_t j = 0; j < p.second.size(); ++j) {
      if (j)
        s += ",";
      s += std::to_string(p.second[j]);
    }
    s += "]}";
  }
  s += "],\"correlation_evidence\":[";
  first = true;
  for (const auto &p : evidence(r.events)) {
    if (!first)
      s += ",";
    first = false;
    s += "{\"identifier\":" + quote(p.first) + ",\"event_indices\":[";
    for (size_t j = 0; j < p.second.size(); ++j) {
      if (j)
        s += ",";
      s += std::to_string(p.second[j]);
    }
    s += "]}";
  }
  s += "],\"nearby\":{";
  for (int side = 0; side < 3; ++side) {
    if (side)
      s += ",";
    s += quote(side == 0   ? "preceding"
               : side == 1 ? "following"
                           : "same_time") +
         ":[";
    first = true;
    if (r.selected)
      for (size_t i = 0; i < r.events.size(); ++i) {
        const auto &e = r.events[i];
        if (field(e, "channel") == field(*r.selected, "channel") &&
            field(e, "record_id") == field(*r.selected, "record_id"))
          continue;
        if ((side == 0 && e.ticks < r.selected->ticks) ||
            (side == 1 && e.ticks > r.selected->ticks) ||
            (side == 2 && e.ticks == r.selected->ticks)) {
          if (!first)
            s += ",";
          first = false;
          s += std::to_string(i);
        }
      }
    s += "]";
  }
  s += "},\"warnings\":[";
  first = true;
  for (const auto &c : r.channels)
    if (c.error || c.truncated || c.status == "event_not_found") {
      if (!first)
        s += ",";
      first = false;
      s += quote(c.name +
                 (c.status == "event_not_found" ? ": requested event not found"
                  : c.status == "data_limit" ? ": retained data limit reached"
                  : c.truncated ? ": event limit reached"
                                : ": acquisition incomplete (error " +
                                      std::to_string(c.error) + ")"));
    }
  return s + "],\"limitations\":[" + quote(limitation) +
         ",\"Absent fields are null and unavailable. "
         "Messages may be unavailable independently of event "
         "acquisition.\"]}\n";
}
std::string text(const Report &r) {
  std::ostringstream s;
  s << "event-correlation-engine 1.0.0\n" << limitation << "\n";
  if (r.options.mode == "around" && !r.selected)
    s << "Window unavailable: selected event unavailable.\n";
  else
    s << "Window: " << timestamp(r.start) << " to " << timestamp(r.end) << "\n";
  for (const auto &c : r.channels)
    s << "Channel " << c.name << ": " << c.status << ", retained " << c.retained
      << ", error " << c.error << (c.truncated ? ", limit reached" : "")
      << "\n";
  if (r.selected)
    s << "Selected: " << object(r.selected->fields) << "\nmessage: "
      << (r.selected->message ? quote(*r.selected->message) : "unavailable")
      << "\n";
  s << "Events retained: " << r.events.size()
    << "\nProvider/event ID/level groups:\n";
  for (const auto &g : groups(r.events, r.options.detailed)) {
    s << g.first << " count=" << g.second.size()
      << " first=" << timestamp(r.events[g.second.front()].ticks)
      << " last=" << timestamp(r.events[g.second.back()].ticks) << "\n";
    std::map<std::string, bool> channels;
    std::optional<std::string> message;
    size_t metadata = 0;
    for (auto i : g.second) {
      const auto &e = r.events[i];
      channels[field(e, "channel")] = true;
      if (!message && e.message)
        message = e.message;
      if (!field(e, "activity_id").empty() || !field(e, "process_id").empty())
        ++metadata;
    }
    s << "  channels=";
    for (const auto &c : channels)
      s << quote(c.first) << ' ';
    s << "events with activity/process metadata=" << metadata
      << "\n  representative message: "
      << (message ? quote(*message) : "unavailable") << "\n";
  }
  s << "Temporal clusters (inclusive adjacent gap " << r.options.gap
    << " seconds):\n";
  for (const auto &c : clusters(r.events, r.options.gap))
    s << "count=" << c.size() << " " << timestamp(r.events[c.front()].ticks)
      << " to " << timestamp(r.events[c.back()].ticks) << "\n";
  s << "Available correlation identifiers:\n";
  for (const auto &p : evidence(r.events))
    s << p.first << " events=" << p.second.size() << "\n";
  s << "Recurring sets of groups in temporal windows:\n";
  for (const auto &p :
       recurringPatterns(r.events, r.options.gap, r.options.detailed))
    s << p.first << " windows=" << p.second.size() << "\n";
  if (r.selected) {
    s << "Identifiers shared with selected event:\n";
    for (const auto &p : selectedEvidence(r))
      s << p.first << " nearby events=" << p.second.size() << "\n";
  }
  if (r.options.mode != "groups")
    for (const auto &e : r.events) {
      s << (r.selected ? (e.ticks < r.selected->ticks   ? "preceding "
                          : e.ticks > r.selected->ticks ? "following "
                                                        : "same_time ")
                       : "")
        << object(e.fields)
        << "\nmessage: " << (e.message ? quote(*e.message) : "unavailable")
        << "\n";
    }
  s << "Use --json for structured event data, metadata availability and event "
       "membership.\n";
  return s.str();
}
} // namespace ece
