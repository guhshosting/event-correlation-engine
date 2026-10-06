#include "engine.h"
#include <cstdio>
#include <iostream>
#include <stdexcept>
namespace {
struct Token {
  HANDLE h = nullptr;
  ~Token() {
    if (h)
      CloseHandle(h);
  }
};
struct Impersonation {
  ~Impersonation() { RevertToSelf(); }
};
void check(bool value, const char *name) {
  if (!value)
    throw std::runtime_error(name);
}
} // namespace
int main() {
  try {
    Token original, restricted;
    check(OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY,
                           &original.h) != 0,
          "open test token");
    BYTE admin[SECURITY_MAX_SID_SIZE]{};
    DWORD bytes = sizeof(admin);
    check(CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admin,
                             &bytes) != 0,
          "admin SID");
    SID_AND_ATTRIBUTES deny{admin, 0};
    check(CreateRestrictedToken(original.h, DISABLE_MAX_PRIVILEGE, 1, &deny, 0,
                                nullptr, 0, nullptr, &restricted.h) != 0,
          "restricted test token");
    check(ImpersonateLoggedOnUser(restricted.h) != 0, "test impersonation");
    Impersonation guard;
    ece::Options o;
    o.channels = {L"Security"};
    o.minutes = 1;
    o.maxEvents = 1;
    auto denied = ece::acquire(o);
    check(denied.exitCode == 4 && denied.channels[0].status == "access_denied",
          "restricted Security access");
    o.channels = {L"System", L"Security"};
    auto partial = ece::acquire(o);
    check(partial.exitCode == 0 && partial.channels[0].status == "complete" &&
              partial.channels[1].error == ERROR_ACCESS_DENIED,
          "ordinary System and partial Security");
    check(partial.events.size() <= 1, "live event bound");
    std::cout << "Restricted-token read-only live tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  } catch (...) {
    return 1;
  }
}
