#pragma once

#include <string>
#include <optional>
#include <memory>
#include <chrono>

namespace waybar::util {

struct ThumbnailMetadata {
  std::string windowAddress;
  std::string windowClass;
  std::string windowTitle;
  std::string workspaceName;
  std::chrono::system_clock::time_point timestamp;
  int width;
  int height;
};

class ThumbnailCache {
 public:
  ThumbnailCache();
  ~ThumbnailCache() = default;

  // Capture thumbnail for a window (async, non-blocking)
  void captureWindow(const std::string& windowAddress, int x, int y, int width, int height,
                     const std::string& windowClass, const std::string& windowTitle,
                     const std::string& workspaceName);

  // Capture thumbnail synchronously (blocking, for immediate capture before workspace switch)
  void captureWindowSync(const std::string& windowAddress, int x, int y, int width, int height,
                         const std::string& windowClass, const std::string& windowTitle,
                         const std::string& workspaceName);

  // Get cached thumbnail path (returns empty if not cached or too old)
  std::optional<std::string> getThumbnailPath(const std::string& windowAddress,
                                              int maxAgeSeconds = 300);

  // Get thumbnail metadata
  std::optional<ThumbnailMetadata> getMetadata(const std::string& windowAddress);

  // Clear old thumbnails (LRU cleanup)
  void cleanup(int maxAgeSeconds = 3600, size_t maxSizeMB = 100);

  // Check if capture tools are available
  bool isAvailable() const { return m_captureAvailable; }

 private:
  std::string getCachePath() const;
  std::string getThumbnailFilePath(const std::string& windowAddress) const;
  std::string getMetadataFilePath(const std::string& windowAddress) const;
  
  bool m_captureAvailable;
  std::string m_cacheDir;
  
  // Helper to check for grim and magick/convert
  bool checkCaptureTools();
  std::string getResizeCommand() const;
};

}  // namespace waybar::util
