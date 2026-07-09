#pragma once

#include "mining.h"

#include "esphome/core/component.h"
#include "esphome/core/defines.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#ifdef USE_OTA_STATE_LISTENER
#include "esphome/components/ota/ota_backend.h"
#endif

#if defined(USE_ESP32)
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/soc_caps.h>
#endif

#if defined(USE_ESP8266)
#define SOC_CPU_CORES_NUM 1
#include "esphome/components/http_request/http_request.h"
#endif

namespace esphome::duco {

static const char *const TAG = "duco";
static const char *const DUCO_VERSION = "2026.7.1";

inline constexpr uint32_t CHECK_INTERVAL = 60000;
inline constexpr uint32_t UPDATE_INTERVAL = 15000;

struct MiningConfig {
  std::string DUCO_USER = "";
  std::string RIG_IDENTIFIER = "";
  std::string MINER_KEY = "";
  uint16_t WALLET_ID = 0;

  std::string chip_id = "";
  std::string node_id = "";
  std::string host = "";
  uint16_t port = 0;

  std::atomic<bool> is_ready{false};

  MiningConfig(std::string DUCO_USER, std::string RIG_IDENTIFIER, std::string MINER_KEY)
      : DUCO_USER(DUCO_USER), RIG_IDENTIFIER(RIG_IDENTIFIER), MINER_KEY(MINER_KEY) {}
};

class Duco : public Component
#ifdef USE_OTA_STATE_LISTENER
    ,
             public ota::OTAGlobalStateListener
#endif
{
 public:
  float get_setup_priority() const override { return setup_priority::LATE; }

  void setup() override;
  void start();
  void stop();
  void loop() override;

  void dump_config() override;

  void set_username(const char *username) { this->username_ = username; }
  void set_key(const char *key) { this->key_ = key; }
  void set_worker(const char *worker) { this->worker_ = worker; }

#ifdef USE_WEBSERVER
  void sync_web_config(const std::string &web_user, const std::string &web_key, const std::string &web_worker);
#endif

#ifdef USE_SENSOR
  void set_temperature_sensor(sensor::Sensor *temperature_sensor) { this->temperature_sensor_ = temperature_sensor; }
  void set_humidity_sensor(sensor::Sensor *humidity_sensor) { this->humidity_sensor_ = humidity_sensor; }
  void set_cputemp_sensor(sensor::Sensor *cputemp_sensor) { this->cputemp_sensor_ = cputemp_sensor; }
#endif

#ifdef USE_BINARY_SENSOR
  void set_status(binary_sensor::BinarySensor *status) { this->status_ = status; }
#endif
#ifdef USE_SENSOR
  void set_hashrate(sensor::Sensor *hashrate) { hashrate_ = hashrate; }
  void set_accepted_shares(sensor::Sensor *accepted_shares) { accepted_shares_ = accepted_shares; }
  void set_total_shares(sensor::Sensor *total_shares) { total_shares_ = total_shares; }
  void set_difficulty(sensor::Sensor *difficulty) { difficulty_ = difficulty; }
  void set_share_rate(sensor::Sensor *share_rate) { share_rate_ = share_rate; }
  void set_accept_rate(sensor::Sensor *accept_rate) { accept_rate_ = accept_rate; }
  void set_ping(sensor::Sensor *ping) { ping_ = ping; }
#endif
#ifdef USE_TEXT_SENSOR
  void set_pool(text_sensor::TextSensor *pool) { pool_ = pool; }
  void set_cores_status(text_sensor::TextSensor *cores_status) { cores_status_ = cores_status; }
#endif

#ifdef USE_OTA_STATE_LISTENER
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error, ota::OTAComponent *comp) override;
#endif

#if defined(USE_ESP8266)
  void set_http_request(http_request::HttpRequestComponent *http_request_comp) {
    this->http_request_comp_ = http_request_comp;
  }
  http_request::HttpRequestComponent *get_http_request_comp() { return this->http_request_comp_; }
#endif

  void check_for_problem();

  void on_share_found_callback();

  template<typename F> void add_on_share_found_callback(F &&callback) {
    this->share_found_callback.add(std::forward<F>(callback));
  }

#ifdef USE_SENSOR
  std::string get_temperature_string() const;
  std::string get_humidity_string() const;
  std::string get_cputemp_string() const;
#endif

 protected:
  const char *username_{nullptr};
  const char *key_{nullptr};
  const char *worker_{nullptr};

  uint32_t last_fetch_time_{0};
  uint32_t last_check_time_{0};
  uint32_t last_sensor_update_{0};

#if defined(USE_ESP32)
  TaskHandle_t miner_handles[SOC_CPU_CORES_NUM]{nullptr};
#endif

  MiningConfig *configuration{nullptr};
  MiningJob *job[SOC_CPU_CORES_NUM]{nullptr};

#ifdef USE_SENSOR
  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *humidity_sensor_{nullptr};
  sensor::Sensor *cputemp_sensor_{nullptr};
#endif

#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *status_{nullptr};
#endif
#ifdef USE_SENSOR
  sensor::Sensor *hashrate_{nullptr};
  sensor::Sensor *accepted_shares_{nullptr};
  sensor::Sensor *total_shares_{nullptr};
  sensor::Sensor *difficulty_{nullptr};
  sensor::Sensor *share_rate_{nullptr};
  sensor::Sensor *accept_rate_{nullptr};
  sensor::Sensor *ping_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *pool_{nullptr};
  text_sensor::TextSensor *cores_status_{nullptr};
#endif

  void update_sensors();
  bool fetch_pool_node();
  void generate_identifier();

#if defined(USE_ESP32)
  static void duco_thread_entry(void *params);
#endif

#if defined(USE_ESP8266)
  http_request::HttpRequestComponent *http_request_comp_{nullptr};
#endif

  CallbackManager<void()> share_found_callback;
};  // Duco

}  // namespace esphome::duco
