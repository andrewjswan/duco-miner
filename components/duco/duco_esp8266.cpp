#include "duco.h"
#include "mining.h"

#include "esphome/core/application.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "esphome/components/http_request/http_request.h"
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

void Duco::loop() {
  this->update_sensors();

  if (!network::is_connected()) {
    this->configuration->is_ready = false;
    return;
  }

  if ((this->username_ == nullptr || strlen(this->username_) == 0) ||
      (this->worker_ == nullptr || strlen(this->worker_) == 0) ||
      (this->key_ == nullptr || strlen(this->key_) == 0)) {
    this->configuration->is_ready = false;
    return;
  }

  uint32_t current_time = millis();
  if (current_time - this->last_check_time_ >= CHECK_INTERVAL) {
    this->last_check_time_ = current_time;
    check_for_problem();
  }

#if defined(ESP8266)
  if (this->configuration->is_ready && this->job[0] != nullptr) {
    this->job[0]->mine();
  }
#endif

  if (this->configuration->is_ready)
    return;

  if (this->last_fetch_time_ != 0 && (current_time - this->last_fetch_time_ < CHECK_INTERVAL)) {
    return;
  }
  this->last_fetch_time_ = current_time;
  this->fetch_pool_node();
}

void Duco::start() {
  this->job[0] = new MiningJob(0, this->configuration, this);

  ESP_LOGCONFIG(TAG, "Duco started...");
}  // start()

void Duco::stop() {
  if (this->configuration != nullptr) {
    this->configuration->is_ready = false;
  }

  if (this->job[0] != nullptr) {
    delete this->job[0];
    this->job[0] = nullptr;
  }
  esphome::delay(50);

  ESP_LOGCONFIG(TAG, "Duco stopped.");
}  // stop()

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
  // 1. Critical safety check: ensure the configuration structure itself is allocated.
  // This completely eliminates any risk of Guru Meditation / Core Crash during boot cycles.
  if (this->configuration == nullptr) {
    ESP_LOGW(TAG, "Config sync aborted: 'configuration' is not initialized yet.");
    return;
  }

  bool changed = false;

  // 2. Safe check and sync for Username
  // Verify that the incoming raw web string pointer is valid and contains data
  if (this->username_ != nullptr && std::strlen(this->username_) > 0) {
    if (this->configuration->DUCO_USER != this->username_) {
      this->configuration->DUCO_USER = this->username_;
      changed = true;
      ESP_LOGI(TAG, "Config synced: Username set to %s", this->username_);
    }
  } else {
    ESP_LOGD(TAG, "Config sync: Username is empty, keeping default.");
  }

  // 3. Safe check and sync for Miner Key
  // Key can be legally empty on some pools, so we only guard against bad memory addresses
  if (this->key_ != nullptr) {
    if (this->configuration->MINER_KEY != this->key_) {
      this->configuration->MINER_KEY = this->key_;
      changed = true;
      ESP_LOGI(TAG, "Config synced: Miner Key updated.");
    }
  }

  // 4. Safe check and sync for Worker / Rig Identifier
  if (this->worker_ != nullptr && std::strlen(this->worker_) > 0) {
    if (this->configuration->RIG_IDENTIFIER != this->worker_) {
      this->configuration->RIG_IDENTIFIER = this->worker_;
      changed = true;
      ESP_LOGI(TAG, "Config synced: Worker set to %s", this->worker_);
    }
  } else {
    ESP_LOGD(TAG, "Config sync: Worker is empty, keeping default.");
  }

  // 5. Force miner socket session restart if any configuration parameter has mutated.
  // This breaks the current TCP link and forces threads to trigger reconnect loops instantly.
  if (changed) {
    this->generate_identifier();
    this->configuration->is_ready = false;
  }
}

bool Duco::fetch_pool_node() {
  ESP_LOGI(TAG, "Fetching active node from Poolpicker...");
  this->configuration->is_ready = false;

  if (this->http_request_comp_ == nullptr) {
    ESP_LOGE(TAG, "http_request reference is missing.");
    return false;
  }

  std::vector<esphome::http_request::Header> headers;
  esphome::http_request::Header accept_header;
  accept_header.name = "Accept";
  accept_header.value = "application/json";
  headers.push_back(accept_header);

  std::string url = "https://server.duinocoin.com/getPool";
  auto container = this->http_request_comp_->get(url, headers);
  if (container == nullptr)
    return false;

  std::string json_body = "";
  uint8_t recv_buffer[128];
  int16_t bytes_read = 0;

  while ((bytes_read = container->read(recv_buffer, sizeof(recv_buffer) - 1)) > 0) {
    json_body.append(reinterpret_cast<char *>(recv_buffer), bytes_read);
    if (json_body.length() > 3072)
      return false;
  }

  if (json_body.empty())
    return false;

  return esphome::json::parse_json(json_body, [this](JsonObject root) -> bool {
    if (root["ip"].is<JsonVariant>() && root["port"].is<JsonVariant>()) {
      this->configuration->host = root["ip"].as<std::string>();
      this->configuration->port = root["port"].as<uint16_t>();
      this->configuration->node_id = root["name"].is<JsonVariant>() ? root["name"].as<std::string>() : "DucoNode";
      this->configuration->is_ready = true;
      return true;
    }
    return false;
  });
}

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

void Duco::update_sensors() {
  uint32_t current_time = millis();
  bool is_system_ready = this->configuration->is_ready;

#ifdef USE_BINARY_SENSOR
  if (this->status_ != nullptr && this->status_->state != is_system_ready) {
    this->status_->publish_state(is_system_ready);
  }
#endif

  if (this->last_sensor_update_ == 0 || (current_time - this->last_sensor_update_ >= UPDATE_INTERVAL)) {
    this->last_sensor_update_ = current_time;

#ifdef USE_TEXT_SENSOR
    if (this->cores_status_ != nullptr) {
      std::string current_cores_status = "";
      current_cores_status.reserve(SOC_CPU_CORES_NUM);

      for (uint8_t i = 0; i < SOC_CPU_CORES_NUM; i++) {
        if (this->job[i] == nullptr) {
          current_cores_status += "-";
        } else if (this->job[i]->problem()) {
          current_cores_status += "X";
        } else {
          current_cores_status += "*";
        }
      }
      if (this->cores_status_->state != current_cores_status) {
        this->cores_status_->publish_state(current_cores_status);
      }
    }
#endif

#ifdef USE_SENSOR
    if (!is_system_ready) {
      if (this->hashrate_ != nullptr && !std::isnan(this->hashrate_->state)) {
        this->hashrate_->publish_state(NAN);
      }
      if (this->ping_ != nullptr && !std::isnan(this->ping_->state)) {
        this->ping_->publish_state(NAN);
      }
      if (this->share_rate_ != nullptr && !std::isnan(this->share_rate_->state)) {
        this->share_rate_->publish_state(NAN);
      }
      return;
    }

    uint32_t total_hashrate = 0U;
    uint32_t total_accepted = 0U;
    uint32_t total_shares_count = 0U;
    uint32_t max_ping = 0U;
    uint32_t current_diff = 0U;

    for (uint8_t i = 0; i < SOC_CPU_CORES_NUM; i++) {
      if (this->job[i] != nullptr) {
        total_hashrate += this->job[i]->hashrate;
        total_accepted += this->job[i]->accepted_share_count;
        total_shares_count += this->job[i]->share_count;

        uint32_t j_ping = this->job[i]->ping;
        if (j_ping > max_ping)
          max_ping = j_ping;

        if (current_diff == 0) {
          current_diff = this->job[i]->difficulty;
        }
      }
    }

    if (this->hashrate_ != nullptr) {
      this->hashrate_->publish_state(static_cast<float>(total_hashrate) / 1000.0f);
    }

    if (this->ping_ != nullptr) {
      this->ping_->publish_state(max_ping);
    }

    if (this->accepted_shares_ != nullptr) {
      this->accepted_shares_->publish_state(total_accepted);
    }
    if (this->total_shares_ != nullptr) {
      this->total_shares_->publish_state(total_shares_count);
    }

    if (this->difficulty_ != nullptr && this->difficulty_->state != current_diff) {
      this->difficulty_->publish_state(current_diff);
    }

    if (this->accept_rate_ != nullptr && total_shares_count > 0) {
      float a_rate = (static_cast<float>(total_accepted) / total_shares_count) * 100.0f;
      this->accept_rate_->publish_state(a_rate);
    }
    if (this->share_rate_ != nullptr) {
      float total_secs = static_cast<float>(millis()) / 1000.0f;
      if (total_secs > 0.0f) {
        float sharerate = static_cast<float>(total_shares_count) / total_secs;
        this->share_rate_->publish_state(sharerate);
      } else {
        this->share_rate_->publish_state(0);
      }
    }
#endif  // USE_SENSOR
  }
}

}  // namespace esphome::duco
