#include "duco.h"
#include "mining.h"

#include "esphome/core/application.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"
#include "esphome/components/socket/socket.h"
#include "esphome/components/json/json_util.h"

#include <algorithm>
#include <cctype>

namespace esphome::duco {

#define CHECK_INTERVAL 60000
#define UPDATE_INTERVAL 15000

void Duco::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Duco...");

#ifdef USE_OTA_STATE_LISTENER
  ota::get_global_ota_callback()->add_global_state_listener(this);
#endif

  this->configuration = new MiningConfig(this->username_, this->worker_, this->key_);
  this->configuration->WALLET_ID = random_uint32() % 2811;  // Needed for miner grouping in the wallet
  this->generate_identifier();

  this->start();
}  // setup()

#ifdef USE_OTA_STATE_LISTENER
void Duco::on_ota_global_state(ota::OTAState state, float progress, uint8_t error, ota::OTAComponent *comp) {
  if (state == ota::OTA_STARTED) {
    this->disable_loop();
    this->stop();
  }
}
#endif

void Duco::dump_config() {
  ESP_LOGCONFIG(TAG, "Duco version: %s", DUCO_VERSION);
  ESP_LOGCONFIG(TAG, "      Worker: %s", this->configuration->RIG_IDENTIFIER.c_str());
  ESP_LOGCONFIG(TAG, "       Cores: %d", SOC_CPU_CORES_NUM);
#ifdef OFFICIAL_VERSION
  ESP_LOGCONFIG(TAG, "Mimicry mode: %s", OFFICIAL_VERSION);
#endif

#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Status", this->status_);
#endif

#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Accepted shares", this->accepted_shares_);
  LOG_SENSOR("  ", "Total shares", this->total_shares_);
  LOG_SENSOR("  ", "Difficulty", this->difficulty_);

  LOG_SENSOR("  ", "Share rate", this->share_rate_);
  LOG_SENSOR("  ", "Accept rate", this->accept_rate_);
  LOG_SENSOR("  ", "Ping", this->ping_);
#endif

#ifdef USE_TEXTSENSOR
  LOG_SENSOR("  ", "Pool", this->pool_);
  LOG_SENSOR("  ", "Cores status", this->cores_status_);
#endif
}  // dump_config()

void Duco::update_config() {
  if (this->configuration == nullptr) {
    ESP_LOGW(TAG, "Config sync aborted: 'configuration' is not initialized yet.");
    return;
  }

  bool changed = false;

  if (this->username_ != nullptr && std::strlen(this->username_) > 0) {
    if (this->configuration->DUCO_USER != this->username_) {
      this->configuration->DUCO_USER = this->username_;
      changed = true;
      ESP_LOGI(TAG, "Config synced: Username set to %s", this->username_);
    }
  } else {
    ESP_LOGD(TAG, "Config sync: Username is empty, keeping default.");
  }

  if (this->key_ != nullptr) {
    if (this->configuration->MINER_KEY != this->key_) {
      this->configuration->MINER_KEY = this->key_;
      changed = true;
      ESP_LOGI(TAG, "Config synced: Miner Key updated.");
    }
  }

  if (this->worker_ != nullptr && std::strlen(this->worker_) > 0) {
    if (this->configuration->RIG_IDENTIFIER != this->worker_) {
      this->configuration->RIG_IDENTIFIER = this->worker_;
      changed = true;
      ESP_LOGI(TAG, "Config synced: Worker set to %s", this->worker_);
    }
  } else {
    ESP_LOGD(TAG, "Config sync: Worker is empty, keeping default.");
  }

  if (changed) {
    this->generate_identifier();
    this->configuration->is_ready = false;
  }
}

#ifdef USE_SENSOR
std::string Duco::get_temperature_string() const {
  if (this->temperature_sensor_ != nullptr && this->temperature_sensor_->has_state()) {
    return esphome::str_sprintf("Temp:%.1f%s", this->temperature_sensor_->get_state(),
                                this->temperature_sensor_->get_unit_of_measurement_ref().c_str());
  }
  return "";
}

std::string Duco::get_humidity_string() const {
  if (this->humidity_sensor_ != nullptr && this->humidity_sensor_->has_state()) {
    return esphome::str_sprintf("Hum:%.0f%%", this->humidity_sensor_->get_state());
  }
  return "";
}

std::string Duco::get_cputemp_string() const {
  if (this->cputemp_sensor_ != nullptr && this->cputemp_sensor_->has_state()) {
    return esphome::str_sprintf("CPU Temp:%.1f%s", this->cputemp_sensor_->get_state(),
                                this->cputemp_sensor_->get_unit_of_measurement_ref().c_str());
  }
  return "";
}
#endif

void Duco::generate_identifier() {
  this->configuration->chip_id = esphome::str_upper_case(esphome::get_mac_address());
  if (this->configuration->RIG_IDENTIFIER != "Auto")
    return;
  this->configuration->RIG_IDENTIFIER = std::string(DUCO_CHIP) + "-" + this->configuration->chip_id;
}

void Duco::on_share_found_callback() {
  this->defer([this]() {
    ESP_LOGD(TAG, "Share found event caught in the main ESPHome loop!");
    this->share_found_callback.call();
  });
}

void Duco::check_for_problem() {
  bool should_restart = false;

  for (uint8_t i = 0; i < SOC_CPU_CORES_NUM; i++) {
    if (this->job[i] != nullptr) {
      if (this->job[i]->problem()) {
        ESP_LOGW(TAG, "Miner on Core[%d] has a problem!", i);
        should_restart = true;
      }
    }
  }

  if (should_restart) {
    esphome::delay(1000);
    esphome::App.safe_reboot();
  }
}

}  // namespace esphome::duco
