/*
 * Copyright 2026 LiveKit
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the License governing permissions and limitations.
 */

#include <sstream>
#include <utility>

#include "token_source_internal.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

namespace livekit {
namespace {

#if !defined(_WIN32)
size_t curlWriteCallback(char* contents, size_t size, size_t nmemb, void* user_data) {
  const size_t total_size = size * nmemb;
  auto* response = static_cast<std::string*>(user_data);
  response->append(contents, total_size);
  return total_size;
}
#endif

#if defined(_WIN32)
std::wstring toWide(const std::string& value) {
  if (value.empty()) {
    return L"";
  }
  const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
  if (length <= 0) {
    return L"";
  }
  std::wstring wide(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), wide.data(), length);
  return wide;
}

Result<std::string, std::string> winHttpPost(const std::string& url, const std::map<std::string, std::string>& headers,
                                             const std::string& json_body, std::chrono::milliseconds timeout) {
  URL_COMPONENTS components{};
  components.dwStructSize = sizeof(components);
  components.dwSchemeLength = static_cast<DWORD>(-1);
  components.dwHostNameLength = static_cast<DWORD>(-1);
  components.dwUrlPathLength = static_cast<DWORD>(-1);
  components.dwExtraInfoLength = static_cast<DWORD>(-1);

  const std::wstring wide_url = toWide(url);
  if (!WinHttpCrackUrl(wide_url.c_str(), 0, 0, &components)) {
    return Result<std::string, std::string>::failure("failed to parse token server URL");
  }

  const std::wstring host(components.lpszHostName, components.dwHostNameLength);
  const std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);

  HINTERNET session = WinHttpOpen(L"LiveKit-CPP/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, 0);
  if (session == nullptr) {
    return Result<std::string, std::string>::failure("WinHttpOpen failed");
  }

  const int timeout_ms = static_cast<int>(timeout.count());
  WinHttpSetTimeouts(session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

  HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
  if (connection == nullptr) {
    WinHttpCloseHandle(session);
    return Result<std::string, std::string>::failure("WinHttpConnect failed");
  }

  const DWORD flags = (components.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request = WinHttpOpenRequest(connection, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (request == nullptr) {
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return Result<std::string, std::string>::failure("WinHttpOpenRequest failed");
  }

  std::wstring header_block = L"Content-Type: application/json\r\n";
  for (const auto& [key, value] : headers) {
    header_block += toWide(key);
    header_block += L": ";
    header_block += toWide(value);
    header_block += L"\r\n";
  }

  const BOOL send_ok =
      WinHttpSendRequest(request, header_block.c_str(), static_cast<DWORD>(-1L), const_cast<char*>(json_body.data()),
                         static_cast<DWORD>(json_body.size()), static_cast<DWORD>(json_body.size()), 0);
  if (!send_ok) {
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return Result<std::string, std::string>::failure("WinHttpSendRequest failed");
  }

  if (!WinHttpReceiveResponse(request, nullptr)) {
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return Result<std::string, std::string>::failure("WinHttpReceiveResponse failed");
  }

  DWORD status_code = 0;
  DWORD status_size = sizeof(status_code);
  WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);

  std::string response_body;
  DWORD available = 0;
  do {
    if (!WinHttpQueryDataAvailable(request, &available)) {
      break;
    }
    if (available == 0) {
      break;
    }

    std::string chunk(available, '\0');
    DWORD read = 0;
    if (!WinHttpReadData(request, chunk.data(), available, &read)) {
      break;
    }
    chunk.resize(read);
    response_body += chunk;
  } while (available > 0);

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);

  if (status_code < 200 || status_code >= 300) {
    std::ostringstream message;
    message << "token server HTTP " << status_code << ": " << response_body;
    return Result<std::string, std::string>::failure(message.str());
  }

  return Result<std::string, std::string>::success(std::move(response_body));
}
#endif

} // namespace

Result<std::string, std::string> tokenSourceHttpPost(const std::string& url,
                                                     const std::map<std::string, std::string>& headers,
                                                     const std::string& json_body, std::chrono::milliseconds timeout) {
#if defined(_WIN32)
  return winHttpPost(url, headers, json_body, timeout);
#else
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    return Result<std::string, std::string>::failure("curl_easy_init failed");
  }

  std::string response_body;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(json_body.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout.count()));
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "LiveKit-CPP/1.0");

  struct curl_slist* curl_headers = nullptr;
  curl_headers = curl_slist_append(curl_headers, "Content-Type: application/json");
  for (const auto& [key, value] : headers) {
    const std::string header = key + ": " + value;
    curl_headers = curl_slist_append(curl_headers, header.c_str());
  }
  if (curl_headers != nullptr) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
  }

  const CURLcode perform_result = curl_easy_perform(curl);
  long status_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

  if (curl_headers != nullptr) {
    curl_slist_free_all(curl_headers);
  }
  curl_easy_cleanup(curl);

  if (perform_result != CURLE_OK) {
    return Result<std::string, std::string>::failure(curl_easy_strerror(perform_result));
  }

  if (status_code < 200 || status_code >= 300) {
    std::ostringstream message;
    message << "token server HTTP " << status_code << ": " << response_body;
    return Result<std::string, std::string>::failure(message.str());
  }

  return Result<std::string, std::string>::success(std::move(response_body));
#endif
}

} // namespace livekit
