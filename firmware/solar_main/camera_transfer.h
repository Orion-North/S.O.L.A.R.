#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <esp_camera.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <errno.h>

class SolarWebServer : public WebServer {
 public:
  using WebServer::WebServer;
  // WiFiServer::begin resets this option, so call after server.begin().
  void enableLowLatency() { _server.setNoDelay(true); }
  void clearPendingHeaders() { _responseHeaders = ""; }
  void releaseCaptureClient() {
    // The worker already owns a shared reference. Discard only the server's
    // reference and pending headers, preventing duplicate responses/leaks.
    _currentClient = WiFiClient();
    clearPendingHeaders();
    _chunked = false;
  }
};

// One retained socket, one framebuffer, and no queued images. Authentication
// and pacing stay in the HTTP handler; this task never accesses WebServer.
class CameraTransfer {
 public:
  bool begin() {
    slot_ = xSemaphoreCreateBinary();
    if (!slot_) return false;
    xSemaphoreGive(slot_);
    if (xTaskCreatePinnedToCore(run, "CameraTransfer", 4096, this, 1,
                               &task_, 0) != pdPASS) {
      vSemaphoreDelete(slot_);
      slot_ = nullptr;
      return false;
    }
    return true;
  }

  bool ready() const { return task_ != nullptr; }

  bool submit(const WiFiClient& client, unsigned intervalMs) {
    if (!ready() || xSemaphoreTake(slot_, 0) != pdTRUE) return false;
    // WiFiClient retains its shared socket handle after WebServer releases its
    // reference at the end of handleClient() (Arduino ESP32 2.0.17).
    client_ = client;
    intervalMs_ = intervalMs;
    xTaskNotifyGive(task_);
    return true;
  }

 private:
  SemaphoreHandle_t slot_ = nullptr;
  TaskHandle_t task_ = nullptr;
  WiFiClient client_;
  unsigned intervalMs_ = 50;
  static constexpr uint32_t TRANSFER_TIMEOUT_MS = 2500;

  // WiFiClient::write can wait inside its own retry loop. Use nonblocking
  // socket writes so a slow receiver has a bounded total transfer lifetime.
  static bool sendBytes(int socket, const uint8_t* bytes, size_t length,
                        uint32_t startedAt) {
    size_t sent = 0;
    while (sent < length && millis() - startedAt < TRANSFER_TIMEOUT_MS) {
      size_t chunk = length - sent;
      if (chunk > 1460) chunk = 1460;
      int written = ::send(socket, bytes + sent, chunk, MSG_DONTWAIT);
      if (written > 0) {
        sent += written;
      } else if (written < 0 &&
                 (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
        vTaskDelay(pdMS_TO_TICKS(1));
      } else {
        return false;
      }
    }
    return sent == length;
  }

  static void run(void* context) {
    auto* self = static_cast<CameraTransfer*>(context);
    for (;;) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      self->transfer();
      self->client_.stop();
      xSemaphoreGive(self->slot_);
    }
  }

  void transfer() {
    client_.setNoDelay(true);
    camera_fb_t* frame = esp_camera_fb_get();
    const uint32_t startedAt = millis();
    char header[512];
    int length;
    if (frame) {
      length = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n"
        "Content-Length: %u\r\nAccess-Control-Allow-Origin: *\r\n"
        "Access-Control-Expose-Headers: X-Camera-Interval-Ms,X-Retry-After-Ms,X-Camera-Frame-Ms\r\n"
        "X-Camera-Interval-Ms: %u\r\nX-Camera-Frame-Ms: %lu\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
        static_cast<unsigned>(frame->len), intervalMs_,
        static_cast<unsigned long>(frame->timestamp.tv_sec * 1000ULL +
                                   frame->timestamp.tv_usec / 1000));
      if (length > 0 && static_cast<size_t>(length) < sizeof(header) &&
          sendBytes(client_.fd(), reinterpret_cast<const uint8_t*>(header),
                    length, startedAt)) {
        sendBytes(client_.fd(), frame->buf, frame->len, startedAt);
      }
      // Always return the buffer, including disconnects and deadline expiry.
      esp_camera_fb_return(frame);
    } else {
      const char response[] =
        "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\n"
        "Content-Length: 14\r\nAccess-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\nCapture Failed";
      sendBytes(client_.fd(), reinterpret_cast<const uint8_t*>(response),
                sizeof(response) - 1, startedAt);
    }
  }
};
