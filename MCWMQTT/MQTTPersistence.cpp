/*
** MCWMQTT - Omnis / MQTT Broker Integration
** Persistence-directory helpers
**
** Paths are handled internally as UTF-8. On Windows, directories are created
** through the wide (UTF-16) API, and the path handed to paho is converted to
** the system ANSI code page, because paho's file persistence uses the narrow
** C runtime functions there. On Linux, UTF-8 is the native filesystem
** encoding, so paths are used as-is.
*/

#include "MQTTPersistence.h"
#include <cctype>
#include <cerrno>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>
#endif

namespace {

#ifdef _WIN32
const char kPathSeparator = '\\';
#else
const char kPathSeparator = '/';
#endif

std::string sanitizeForPathComponent(const std::string &value)
{
   std::string result;
   result.reserve(value.size());
   for (char c : value) {
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
         result += c;
      }
      else {
         result += '_';
      }
   }
   if (result.empty()) {
      result = "default";
   }
   return result;
}

#ifdef _WIN32
std::wstring utf8ToWide(const std::string &value)
{
   if (value.empty()) {
      return std::wstring();
   }
   const int len = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
   if (len <= 0) {
      return std::wstring();
   }
   std::wstring result(static_cast<size_t>(len), L'\0');
   MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), &result[0], len);
   return result;
}

std::string wideToUtf8(const std::wstring &value)
{
   if (value.empty()) {
      return std::string();
   }
   const int len = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
   if (len <= 0) {
      return std::string();
   }
   std::string result(static_cast<size_t>(len), '\0');
   WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), &result[0], len, nullptr, nullptr);
   return result;
}

// Converts to the system ANSI code page. Fails if any character cannot be
// represented exactly (no '?' substitution, no best-fit approximations).
bool wideToAnsi(const std::wstring &value, std::string &result)
{
   result.clear();
   if (value.empty()) {
      return true;
   }

   // With the "Use Unicode UTF-8 for worldwide language support" option the
   // ANSI code page is UTF-8, which represents everything; the flags and
   // usedDefaultChar arguments below are not allowed for CP_UTF8.
   if (GetACP() == CP_UTF8) {
      result = wideToUtf8(value);
      return !result.empty();
   }

   BOOL usedDefaultChar = FALSE;
   const int len = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value.data(), static_cast<int>(value.size()),
                                       nullptr, 0, nullptr, &usedDefaultChar);
   if (len <= 0 || usedDefaultChar) {
      return false;
   }
   result.assign(static_cast<size_t>(len), '\0');
   WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, value.data(), static_cast<int>(value.size()),
                       &result[0], len, nullptr, nullptr);
   return true;
}
#endif

// Returns the OS temp directory as UTF-8, with a trailing separator.
std::string getSystemTempDir()
{
#ifdef _WIN32
   wchar_t buffer[MAX_PATH + 1] = { 0 };
   const DWORD len = GetTempPathW(MAX_PATH + 1, buffer);
   if (len > 0 && len <= MAX_PATH) {
      return wideToUtf8(std::wstring(buffer, len));
   }
   return "C:\\Windows\\Temp\\";
#else
   const char *tmpDir = std::getenv("TMPDIR");
   if (tmpDir != nullptr && *tmpDir != '\0') {
      std::string dir(tmpDir);
      if (dir.back() != '/') {
         dir += '/';
      }
      return dir;
   }
   return "/tmp/";
#endif
}

// Creates 'utf8Path' and any missing parent directories.
// Returns false and fills 'error' if the directory does not exist afterwards.
bool createDirectories(const std::string &utf8Path, std::string &error)
{
   if (utf8Path.empty()) {
      error = "The persistence directory is empty.";
      return false;
   }

#ifdef _WIN32
   const std::wstring path = utf8ToWide(utf8Path);
   DWORD lastError = ERROR_SUCCESS;

   // Create each prefix in turn. Failures on intermediate components (drive
   // letters, UNC server/share roots, existing folders) are expected; only
   // the final state of the full path matters.
   for (size_t i = 1; i <= path.size(); ++i) {
      if (i == path.size() || path[i] == L'\\' || path[i] == L'/') {
         const std::wstring prefix = path.substr(0, i);
         if (prefix.back() != L':' && !CreateDirectoryW(prefix.c_str(), nullptr)) {
            const DWORD err = GetLastError();
            if (err != ERROR_ALREADY_EXISTS) {
               lastError = err;
            }
         }
      }
   }

   const DWORD attrs = GetFileAttributesW(path.c_str());
   if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      error = "Unable to create the persistence directory '" + utf8Path + "' (Windows error " +
              std::to_string(lastError != ERROR_SUCCESS ? lastError : GetLastError()) + ").";
      return false;
   }
#else
   int lastError = 0;

   for (size_t i = 1; i <= utf8Path.size(); ++i) {
      if (i == utf8Path.size() || utf8Path[i] == '/') {
         const std::string prefix = utf8Path.substr(0, i);
         if (mkdir(prefix.c_str(), 0700) != 0 && errno != EEXIST) {
            lastError = errno;
         }
      }
   }

   struct stat st;
   if (stat(utf8Path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
      error = "Unable to create the persistence directory '" + utf8Path + "' (" +
              std::strerror(lastError != 0 ? lastError : errno) + ").";
      return false;
   }
#endif

   return true;
}

} // namespace

// Creates the persistence directory and returns, in 'pahoDir', the form of
// the path that paho's file persistence can open on this platform.
bool preparePersistenceDirectory(const std::string &utf8Dir, std::string &pahoDir, std::string &error)
{
   if (!createDirectories(utf8Dir, error)) {
      return false;
   }

#ifdef _WIN32
   const std::wstring widePath = utf8ToWide(utf8Dir);
   if (wideToAnsi(widePath, pahoDir)) {
      return true;
   }

   // Fallback: the 8.3 short form of an existing path avoids the characters
   // the ANSI code page cannot represent (only when 8.3 names are enabled).
   DWORD len = GetShortPathNameW(widePath.c_str(), nullptr, 0);
   if (len > 0) {
      std::wstring shortPath(len, L'\0');
      len = GetShortPathNameW(widePath.c_str(), &shortPath[0], len);
      shortPath.resize(len);
      if (len > 0 && wideToAnsi(shortPath, pahoDir)) {
         return true;
      }
   }

   error = "The persistence directory '" + utf8Dir + "' contains characters not supported by the "
           "system code page. Use $setpersistencedirectory with a path containing only ASCII characters.";
   return false;
#else
   pahoDir = utf8Dir;
   return true;
#endif
}

// Builds the default persistence directory (UTF-8):
// <system temp>/MCWMQTT/<sanitized clientID>/
std::string buildDefaultPersistenceDirectory(const std::string &clientID)
{
   std::string base = getSystemTempDir();
   if (!base.empty() && base.back() != kPathSeparator) {
      base += kPathSeparator;
   }

   return base + "MCWMQTT" + kPathSeparator + sanitizeForPathComponent(clientID) + kPathSeparator;
}
