#include "chrome/install_static/install_modes.h"

#include "chrome/install_static/buildflags.h"

namespace install_static {

namespace {

#if !defined(OFFICIAL_BUILD)
std::wstring GetUnregisteredKeyPathForProduct() {
  return std::wstring(L"Software\\").append(kProductPathName);
}
#endif

}  // namespace

std::wstring GetClientsKeyPath(const wchar_t* app_guid) {
#if defined(OFFICIAL_BUILD)
  return std::wstring(L"Software\\Whist\\Update\\Clients\\").append(app_guid);
#else
  return GetUnregisteredKeyPathForProduct();
#endif
}

std::wstring GetClientStateKeyPath(const wchar_t* app_guid) {
#if defined(OFFICIAL_BUILD)
  return std::wstring(L"Software\\Whist\\Update\\ClientState\\")
      .append(app_guid);
#else
  return GetUnregisteredKeyPathForProduct();
#endif
}

std::wstring GetClientStateMediumKeyPath(const wchar_t* app_guid) {
#if defined(OFFICIAL_BUILD)
  return std::wstring(L"Software\\Whist\\Update\\ClientStateMedium\\")
      .append(app_guid);
#else
  return GetUnregisteredKeyPathForProduct();
#endif
}

}  // namespace install_static
