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

void Duco::loop() {
  this->update_sensors();

  if (!network::is_connected()) {
    this->configuration->is_ready = false;
    return;
  }

#ifdef USE_WEBSERVER
  if (this->configuration->DUCO_USER.empty() ||
      this->configuration->RIG_IDENTIFIER.empty()) {
    this->configuration->is_ready = false;
    return;
  }
#endif

  uint32_t current_time = millis();
  if (current_time - this->last_check_time_ >= CHECK_INTERVAL) {
    this->last_check_time_ = current_time;
    check_for_problem();
  }

  if (this->configuration->is_ready && this->job[0] != nullptr) {
    this->job[0]->mine();
  }

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
