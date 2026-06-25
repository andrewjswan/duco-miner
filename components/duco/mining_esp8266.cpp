#include "counter.h"
#include "duco.h"
#include "mining.h"

#include "esphome/components/socket/socket.h"
#include "esphome/core/application.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#include <algorithm>

namespace esphome::duco {

bool MiningJob::problem() const {
  return this->errors >= ERROR_THRESHOLD;
}

void MiningJob::handleSystemEvents(void) { esphome::App.feed_wdt(); }

void MiningJob::connectToNode() {
  if (!this->config->is_ready)
    return;
  if (this->is_connected && this->client_sock.connected())
    return;

  uint32_t stopWatch = millis();
  ESP_LOGD(TAG, "Core [%d] - Connecting to a Duino-Coin node...", this->core);

  while (true) {
    // Perform standard synchronous client connection over active Wi-Fi/LAN interface
    if (this->client_sock.connect(this->config->host.c_str(), this->config->port)) {
      this->is_connected = true;
      break;
    }

    ESP_LOGW(TAG, "Core [%d] - Connection failed, retrying in 2 seconds...", this->core);
    this->client_sock.stop();
    delay(2000);

    if (millis() - stopWatch > 100000) {
      ESP_LOGE(TAG, "Core [%d] - Critical connection timeout. Resetting configuration...", this->core);
      this->config->is_ready = false;
      delay(100);
      return;
    }
  }

  waitForClientData();
  if (!this->is_connected) {
    this->config->is_ready = false;
    return;
  }

  ESP_LOGI(TAG, "Core [%d] - Connected successfully. Node reported version: %s", this->core,
           this->client_buffer.c_str());
}

void MiningJob::waitForClientData() {
  this->client_buffer = "";
  uint32_t start_time = millis();

  while (this->is_connected) {
    // Stream data out using verified Arduino streams wrapper
    if (this->client_sock.available()) {
      this->client_buffer = this->client_sock.readStringUntil('\n').c_str();
      break;
    }
    if (!this->client_sock.connected()) {
      this->is_connected = false;
      break;
    }
    if (millis() - start_time > 10000) {
      ESP_LOGW(TAG, "Core [%d] - Socket read timeout", this->core);
      this->is_connected = false;
      this->client_sock.stop();
      break;
    }
    delay(1);
  }
}

void MiningJob::submit(uint32_t counter, uint32_t hashrate, float elapsed_time_s) {
  std::string reply = std::to_string(counter);
  reply += SEP_TOKEN;
  reply += std::to_string(hashrate);
  reply += SEP_TOKEN;
  reply += DUCO_MINER_BANNER;
  reply += SPC_TOKEN;
#ifdef OFFICIAL_VERSION
  reply += OFFICIAL_VERSION;
#else
  reply += DUCO_VERSION;
#endif
  reply += SEP_TOKEN;
  reply += this->config->RIG_IDENTIFIER;
  reply += SEP_TOKEN;
  reply += "DUCOID" + this->config->chip_id;
  reply += SEP_TOKEN;
  reply += std::to_string(this->config->WALLET_ID);
  reply += END_TOKEN;

  ESP_LOGD(TAG, "Core [%d] - Sending share to pool: %s", this->core, reply.c_str());

  // Send raw string over the active lwIP stream layout
  int16_t sent_bytes = this->client_sock.write(reply.c_str(), reply.length());
  if (sent_bytes <= 0) {
    ESP_LOGE(TAG, "Core [%d] - Failed to submit share data", this->core);
    this->is_connected = false;
    this->client_sock.stop();
    return;
  }

  uint32_t ping_start = millis();
  waitForClientData();

  this->ping = (uint32_t) (millis() - ping_start);

  bool is_good = (this->client_buffer.find("GOOD") != std::string::npos);
  if (is_good) {
    this->errors = 0;
    this->accepted_share_count++;
  } else {
    this->errors++;
  }

  if (is_good) {
    ESP_LOGI(TAG, "Core [%d] - %s share #%u (%lu) hashrate: %.2f kH/s (%.2fs) Ping: %u ms (%s)", this->core,
             this->client_buffer.c_str(), (unsigned int) this->share_count, counter, hashrate / 1000.0f, elapsed_time_s,
             (unsigned int) this->ping, this->config->node_id.c_str());
  } else {
    ESP_LOGW(TAG, "Core [%d] - %s share #%u (%lu) hashrate: %.2f kH/s (%.2fs) Ping: %u ms (%s)", this->core,
             this->client_buffer.c_str(), (unsigned int) this->share_count, counter, hashrate / 1000.0f, elapsed_time_s,
             (unsigned int) this->ping, this->config->node_id.c_str());
  }
  this->client_buffer.clear();
}

void MiningJob::askForJob() {
  ESP_LOGI(TAG, "Core [%d] - Asking for a new job for user: %s", this->core, this->config->DUCO_USER.c_str());

  std::string job_req = "JOB";
  job_req += SEP_TOKEN;
  job_req += this->config->DUCO_USER;
  job_req += SEP_TOKEN;
  job_req += DUCO_START_DIFF;
  job_req += SEP_TOKEN;
  job_req += this->config->MINER_KEY;

#ifdef USE_SENSOR
  if (this->parent != nullptr) {
    std::string temp_str = this->parent->get_temperature_string();
    std::string hum_str = this->parent->get_humidity_string();
    std::string cpu_str = this->parent->get_cputemp_string();

    if (!temp_str.empty()) {
      job_req += SEP_TOKEN;
      job_req += temp_str;
    }
    if (!hum_str.empty()) {
      job_req += (!temp_str.empty()) ? IOT_TOKEN : SEP_TOKEN;
      job_req += hum_str;
    }
    if (!cpu_str.empty()) {
      job_req += (!temp_str.empty() || !hum_str.empty()) ? IOT_TOKEN : SEP_TOKEN;
      job_req += cpu_str;
    }
  }
#endif

  job_req += END_TOKEN;

  int16_t sent_bytes = this->client_sock.write(job_req.c_str(), job_req.length());
  if (sent_bytes <= 0) {
    ESP_LOGE(TAG, "Core [%d] - Failed to request a new job from node", this->core);
    this->is_connected = false;
    this->client_sock.stop();
    return;
  }
  waitForClientData();

  ESP_LOGD(TAG, "Core [%d] - Received job with size of %d bytes: %s", this->core, (int) this->client_buffer.length(),
           this->client_buffer.c_str());

  if (parse()) {
    ESP_LOGD(TAG, "Core [%d] - Parsed job: %s %s %u", this->core, this->last_block_hash.c_str(),
             this->expected_hash_str.c_str(), (unsigned int) this->difficulty);
  } else {
    this->errors++;
    ESP_LOGE(TAG, "Core [%d] - Job parsing failed!", this->core);
  }
}

}  // namespace esphome::duco
