/*
  Asynchronous WebServer library for Espressif MCUs

  Copyright (c) 2016 Hristo Gochkov. All rights reserved.
  This file is part of the esp8266 core for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#include "ESPAsyncWebServer.h"
#include "WebHandlerImpl.h"

namespace {

constexpr size_t kVoxOneManifestHeaderSize = 64;
constexpr size_t kVoxOneManifestEntrySize = 112;
constexpr size_t kVoxOneManifestPathSize = 64;
constexpr size_t kVoxOneSha256Size = 32;

uint16_t readManifestLe16(const uint8_t* value) {
  return static_cast<uint16_t>(value[0]) |
         (static_cast<uint16_t>(value[1]) << 8);
}

uint32_t readManifestLe32(const uint8_t* value) {
  return static_cast<uint32_t>(value[0]) |
         (static_cast<uint32_t>(value[1]) << 8) |
         (static_cast<uint32_t>(value[2]) << 16) |
         (static_cast<uint32_t>(value[3]) << 24);
}

bool manifestEtag(FS& fs, const String& filename, size_t servedSize,
                  String& etag) {
  File manifest = fs.open("/www/assets.vxwm", "r");
  if (!manifest || manifest.isDirectory()) return false;

  uint8_t header[kVoxOneManifestHeaderSize];
  if (manifest.read(header, sizeof(header)) != sizeof(header) ||
      memcmp(header, "VXWM", 4) != 0 || readManifestLe16(header + 6) != sizeof(header) ||
      readManifestLe16(header + 16) != kVoxOneManifestEntrySize) {
    manifest.close();
    return false;
  }

  const uint16_t count = readManifestLe16(header + 14);
  String manifestPath = filename;
  if (!manifestPath.endsWith(".gz")) manifestPath += ".gz";
  uint8_t record[kVoxOneManifestEntrySize];
  for (uint16_t index = 0; index < count; ++index) {
    if (manifest.read(record, sizeof(record)) != sizeof(record)) break;
    if (record[kVoxOneManifestPathSize - 1] != 0 ||
        manifestPath != reinterpret_cast<const char*>(record) ||
        readManifestLe32(record + kVoxOneManifestPathSize) != servedSize)
      continue;

    static const char hex[] = "0123456789abcdef";
    char quotedSha[kVoxOneSha256Size * 2 + 3];
    quotedSha[0] = '"';
    for (size_t byte = 0; byte < kVoxOneSha256Size; ++byte) {
      const uint8_t value = record[kVoxOneManifestPathSize + 4 + byte];
      quotedSha[1 + byte * 2] = hex[value >> 4];
      quotedSha[2 + byte * 2] = hex[value & 0x0f];
    }
    quotedSha[sizeof(quotedSha) - 2] = '"';
    quotedSha[sizeof(quotedSha) - 1] = 0;
    etag = quotedSha;
    manifest.close();
    return true;
  }
  manifest.close();
  return false;
}

}  // namespace

AsyncStaticWebHandler::AsyncStaticWebHandler(const char* uri, FS& fs, const char* path, const char* cache_control)
  : _fs(fs), _uri(uri), _path(path), _default_file("index.htm"), _cache_control(cache_control), _last_modified(""), _callback(nullptr)
{
  // Ensure leading '/'
  if (_uri.length() == 0 || _uri[0] != '/') _uri = "/" + _uri;
  if (_path.length() == 0 || _path[0] != '/') _path = "/" + _path;

  // If path ends with '/' we assume a hint that this is a directory to improve performance.
  // However - if it does not end with '/' we, can't assume a file, path can still be a directory.
  _isDir = _path[_path.length()-1] == '/';

  // Remove the trailing '/' so we can handle default file
  // Notice that root will be "" not "/"
  if (_uri[_uri.length()-1] == '/') _uri = _uri.substring(0, _uri.length()-1);
  if (_path[_path.length()-1] == '/') _path = _path.substring(0, _path.length()-1);

  // Reset stats
  _gzipFirst = false;
  _gzipStats = 0xF8;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::setIsDir(bool isDir){
  _isDir = isDir;
  return *this;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::setDefaultFile(const char* filename){
  _default_file = String(filename);
  return *this;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::setCacheControl(const char* cache_control){
  _cache_control = String(cache_control);
  return *this;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::setLastModified(const char* last_modified){
  _last_modified = String(last_modified);
  return *this;
}

AsyncStaticWebHandler& AsyncStaticWebHandler::setLastModified(struct tm* last_modified){
  char result[30];
  strftime (result,30,"%a, %d %b %Y %H:%M:%S %Z", last_modified);
  return setLastModified((const char *)result);
}

#ifdef ESP8266
AsyncStaticWebHandler& AsyncStaticWebHandler::setLastModified(time_t last_modified){
  return setLastModified((struct tm *)gmtime(&last_modified));
}

AsyncStaticWebHandler& AsyncStaticWebHandler::setLastModified(){
  time_t last_modified;
  if(time(&last_modified) == 0) //time is not yet set
    return *this;
  return setLastModified(last_modified);
}
#endif
bool AsyncStaticWebHandler::canHandle(AsyncWebServerRequest *request){
  if(request->method() != HTTP_GET 
    || !request->url().startsWith(_uri) 
    || !request->isExpectedRequestedConnType(RCT_DEFAULT, RCT_HTTP)
  ){
    return false;
  }
  if (_getFile(request)) {
    // We interested in "If-Modified-Since" header to check if file was modified
    if (_last_modified.length())
      request->addInterestingHeader("If-Modified-Since");

    if(_cache_control.length())
      request->addInterestingHeader("If-None-Match");

    DEBUGF("[AsyncStaticWebHandler::canHandle] TRUE\n");
    return true;
  }

  return false;
}

bool AsyncStaticWebHandler::_getFile(AsyncWebServerRequest *request)
{
  // Remove the found uri
  String path = request->url().substring(_uri.length());

  // We can skip the file check and look for default if request is to the root of a directory or that request path ends with '/'
  bool canSkipFileCheck = (_isDir && path.length() == 0) || (path.length() && path[path.length()-1] == '/');

  path = _path + path;

  // Do we have a file or .gz file
  if (!canSkipFileCheck && _fileExists(request, path))
    return true;

  // Can't handle if not default file
  if (_default_file.length() == 0)
    return false;

  // Try to add default file, ensure there is a trailing '/' ot the path.
  if (path.length() == 0 || path[path.length()-1] != '/')
    path += "/";
  path += _default_file;

  return _fileExists(request, path);
}

#ifdef ESP32
#define FILE_IS_REAL(f) (f == true && !f.isDirectory())
#else
#define FILE_IS_REAL(f) (f == true)
#endif

bool AsyncStaticWebHandler::_fileExists(AsyncWebServerRequest *request, const String& path)
{
  bool fileFound = false;
  bool gzipFound = false;

  String gzip = path + ".gz";

  if (_gzipFirst) {
    request->_tempFile = _fs.open(gzip, "r");
    gzipFound = FILE_IS_REAL(request->_tempFile);
    if (!gzipFound){
      request->_tempFile = _fs.open(path, "r");
      fileFound = FILE_IS_REAL(request->_tempFile);
    }
  } else {
    request->_tempFile = _fs.open(path, "r");
    fileFound = FILE_IS_REAL(request->_tempFile);
    if (!fileFound){
      request->_tempFile = _fs.open(gzip, "r");
      gzipFound = FILE_IS_REAL(request->_tempFile);
    }
  }

  bool found = fileFound || gzipFound;

  if (found) {
    // Extract the file name from the path and keep it in _tempObject
    size_t pathLen = path.length();
    char * _tempPath = (char*)malloc(pathLen+1);
    snprintf(_tempPath, pathLen+1, "%s", path.c_str());
    request->_tempObject = (void*)_tempPath;

    // Calculate gzip statistic
    _gzipStats = (_gzipStats << 1) + (gzipFound ? 1 : 0);
    if (_gzipStats == 0x00) _gzipFirst = false; // All files are not gzip
    else if (_gzipStats == 0xFF) _gzipFirst = true; // All files are gzip
    else _gzipFirst = _countBits(_gzipStats) > 4; // IF we have more gzip files - try gzip first
  }

  return found;
}

uint8_t AsyncStaticWebHandler::_countBits(const uint8_t value) const
{
  uint8_t w = value;
  uint8_t n;
  for (n=0; w!=0; n++) w&=w-1;
  return n;
}

void AsyncStaticWebHandler::handleRequest(AsyncWebServerRequest *request)
{
  // Get the filename from request->_tempObject and free it
  String filename = String((char*)request->_tempObject);
  free(request->_tempObject);
  request->_tempObject = NULL;
  if((_username != "" && _password != "") && !request->authenticate(_username.c_str(), _password.c_str()))
      return request->requestAuthentication();

  if (request->_tempFile == true) {
    String etag;
    manifestEtag(_fs, filename, request->_tempFile.size(), etag);
    if (_last_modified.length() && _last_modified == request->header("If-Modified-Since")) {
      request->_tempFile.close();
      request->send(304); // Not modified
    } else if (_cache_control.length() && etag.length() && request->hasHeader("If-None-Match") && request->header("If-None-Match").equals(etag)) {
      request->_tempFile.close();
      AsyncWebServerResponse * response = new AsyncBasicResponse(304); // Not modified
      response->addHeader("Cache-Control", _cache_control);
      response->addHeader("ETag", etag);
      request->send(response);
    } else {
      AsyncWebServerResponse * response = new AsyncFileResponse(request->_tempFile, filename, String(), false, _callback);
      if (_last_modified.length())
        response->addHeader("Last-Modified", _last_modified);
      if (_cache_control.length()){
        response->addHeader("Cache-Control", _cache_control);
        if (etag.length()) response->addHeader("ETag", etag);
      }
      request->send(response);
    }
  } else {
    request->send(404);
  }
}
