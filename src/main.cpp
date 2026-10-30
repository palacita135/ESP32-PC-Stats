#include <Arduino.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>

namespace {

constexpr uint32_t SERIAL_BAUD = 115200;
constexpr size_t MAX_LINE_LENGTH = 512;
constexpr uint32_t STALE_AFTER_MS = 5000;

TFT_eSPI display(320, 480);
char lineBuffer[MAX_LINE_LENGTH];
size_t lineLength = 0;
uint32_t lastSampleMs = 0;
bool haveSample = false;
bool invalidFrame = false;

struct Metric {
  float value;
  bool available;
};

struct Stats {
  Metric cpu;
  Metric memory;
  Metric memoryUsed;
  Metric memoryTotal;
  Metric gpu;
  Metric gpuTemperature;
  Metric gpuMemory;
  Metric gpuMemoryTotal;
  Metric download;
  Metric upload;
  Metric uptime;
};

Metric metricFrom(JsonVariantConst value) {
  if (value.is<float>() && isfinite(value.as<float>())) {
    return {value.as<float>(), true};
  }
  return {0.0f, false};
}

bool readMetrics(const char* line, Stats& stats) {
  JsonDocument document;
  const DeserializationError error = deserializeJson(document, line);
  if (error) {
    return false;
  }

  const char* required[] = {"cpu", "mem", "mem_used", "mem_total", "down", "up", "uptime"};
  for (const char* key : required) {
    if (!document[key].is<float>() && !document[key].is<int>()) {
      return false;
    }
  }

  stats.cpu = metricFrom(document["cpu"]);
  stats.memory = metricFrom(document["mem"]);
  stats.memoryUsed = metricFrom(document["mem_used"]);
  stats.memoryTotal = metricFrom(document["mem_total"]);
  stats.gpu = metricFrom(document["gpu"]);
  stats.gpuTemperature = metricFrom(document["gpu_temp"]);
  stats.gpuMemory = metricFrom(document["gpu_mem"]);
  stats.gpuMemoryTotal = metricFrom(document["gpu_mem_total"]);
  stats.download = metricFrom(document["down"]);
  stats.upload = metricFrom(document["up"]);
  stats.uptime = metricFrom(document["uptime"]);
  return true;
}

String formatMetric(const Metric& metric, unsigned int decimals = 0) {
  if (!metric.available) {
    return "N/A";
  }
  return String(metric.value, decimals);
}

String formatRate(const Metric& metric) {
  if (!metric.available) {
    return "N/A";
  }
  const float megabytes = metric.value / (1024.0f * 1024.0f);
  if (megabytes >= 1.0f) {
    return String(megabytes, 1) + " MB/s";
  }
  return String(metric.value / 1024.0f, 0) + " KB/s";
}

void drawBar(int x, int y, int width, const Metric& value, uint16_t color) {
  display.drawRect(x, y, width, 9, TFT_DARKGREY);
  if (!value.available) {
    return;
  }
  const float bounded = constrain(value.value, 0.0f, 100.0f);
  const int fillWidth = static_cast<int>((width - 2) * bounded / 100.0f);
  display.fillRect(x + 1, y + 1, fillWidth, 7, color);
}

void drawMetric(int x, int y, const char* title, const Metric& value,
                const String& suffix, uint16_t color) {
  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString(title, x, y, 2);
  display.setTextColor(color, TFT_BLACK);
  display.drawString(formatMetric(value) + suffix, x, y + 21, 4);
  drawBar(x, y + 54, 210, value, color);
}

void drawDashboard(const Stats& stats) {
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_CYAN, TFT_BLACK);
  display.drawString("PC PERFORMANCE", 12, 8, 2);
  display.drawFastHLine(12, 31, 456, TFT_DARKGREY);

  drawMetric(16, 43, "CPU", stats.cpu, "%", TFT_CYAN);
  drawMetric(254, 43, "MEMORY", stats.memory, "%", TFT_GREEN);
  drawMetric(16, 119, "GPU", stats.gpu, "%", TFT_ORANGE);
  drawMetric(254, 119, "GPU TEMP", stats.gpuTemperature, " C", TFT_YELLOW);

  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString("RAM " + formatMetric(stats.memoryUsed, 1) + "/" +
                         formatMetric(stats.memoryTotal, 1) + " GB",
                     16, 192, 2);
  display.drawString("VRAM " + formatMetric(stats.gpuMemory, 1) + "/" +
                         formatMetric(stats.gpuMemoryTotal, 1) + " GB",
                     254, 192, 2);
  display.drawString("DOWN " + formatRate(stats.download), 16, 222, 2);
  display.drawString("UP   " + formatRate(stats.upload), 254, 222, 2);

  const uint32_t seconds = stats.uptime.available
                               ? static_cast<uint32_t>(stats.uptime.value)
                               : 0;
  char uptime[32];
  snprintf(uptime, sizeof(uptime), "UPTIME %02lu:%02lu:%02lu",
           static_cast<unsigned long>(seconds / 3600),
           static_cast<unsigned long>((seconds / 60) % 60),
           static_cast<unsigned long>(seconds % 60));
  display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  display.drawString(uptime, 16, 278, 2);
  display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  display.drawRightString("USB SERIAL", 464, 278, 2);
}

void drawWaiting(const char* message, uint16_t color) {
  display.fillScreen(TFT_BLACK);
  display.setTextColor(TFT_CYAN, TFT_BLACK);
  display.drawCentreString("PC PERFORMANCE", display.width() / 2, 112, 4);
  display.setTextColor(color, TFT_BLACK);
  display.drawCentreString(message, display.width() / 2, 166, 2);
  display.setTextColor(TFT_DARKGREY, TFT_BLACK);
  display.drawCentreString("USB serial  |  115200 baud", display.width() / 2, 205, 2);
}

void processLine() {
  lineBuffer[lineLength] = '\0';
  Stats next{};
  if (readMetrics(lineBuffer, next)) {
    drawDashboard(next);
    lastSampleMs = millis();
    haveSample = true;
    invalidFrame = false;
  } else {
    invalidFrame = true;
  }
  lineLength = 0;
}

}  // namespace

void setup() {
  Serial.begin(SERIAL_BAUD);
  display.init();
  display.setRotation(1);
  display.fillScreen(TFT_BLACK);
  drawWaiting("WAITING FOR PC", TFT_YELLOW);
}

void loop() {
  while (Serial.available() > 0) {
    const char character = static_cast<char>(Serial.read());
    if (character == '\n') {
      processLine();
    } else if (character != '\r') {
      if (lineLength < MAX_LINE_LENGTH - 1) {
        lineBuffer[lineLength++] = character;
      } else {
        lineLength = 0;
        invalidFrame = true;
      }
    }
  }

  static uint32_t lastStatusDrawMs = 0;
  const uint32_t now = millis();
  if (!haveSample && invalidFrame && now - lastStatusDrawMs >= 1000) {
    drawWaiting("INVALID DATA", TFT_RED);
    lastStatusDrawMs = now;
  } else if (haveSample && now - lastSampleMs > STALE_AFTER_MS &&
             now - lastStatusDrawMs >= 1000) {
    drawWaiting("PC CONNECTION LOST", TFT_RED);
    lastStatusDrawMs = now;
  }
}
