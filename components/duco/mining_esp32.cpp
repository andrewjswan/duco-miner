#include "counter.h"
#include "duco.h"
#include "mining.h"

#include "esphome/components/socket/socket.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#include <algorithm>

namespace esphome::duco {

void MiningJob::handleSystemEvents(void) {
  // Critical: Suspend the current thread for 10ms to let ESPHome run its automation loops.
  // pdMS_TO_TICKS(10) ensures that the tick-rate conversion is accurate across different ESP32 variants.
  vTaskDelay(pdMS_TO_TICKS(10));
}

void MiningJob::connectToNode() {
  if (!this->config->is_ready)
    return;
  if (this->is_connected && this->client_sock)
    return;

  uint32_t stopWatch = millis();
  ESP_LOGD(TAG, "Core [%d] - Connecting to a Duino-Coin node...", this->core);

  while (true) {
    // Create a native blockable ESPHome socket
    this->client_sock = esphome::socket::socket_ip(SOCK_STREAM, 0);
    this->client_sock->setblocking(true);

    struct sockaddr_in server_addr;
    socklen_t len = esphome::socket::set_sockaddr((struct sockaddr *) &server_addr, sizeof(server_addr),
                                                  this->config->host.c_str(), this->config->port);

    // Try to connect using ESPHome socket API
    if (this->client_sock->connect((struct sockaddr *) &server_addr, len) == 0) {
      // Connection successful!
      this->is_connected = true;
      break;
    }

    // If connection failed, log it, free the socket pointer and sleep for 2 seconds
    ESP_LOGW(TAG, "Core [%d] - Connection failed, retrying in 2 seconds...", this->core);
    this->client_sock.reset();
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Global watchdog protection: if node is totally down for 100 seconds, reseting configuration
    if (millis() - stopWatch > 100000) {
      ESP_LOGE(TAG, "Core [%d] - Critical connection timeout. Resetting configuration...", this->core);
      this->config->is_ready = false;  // Invalidate config so loop() can fetch a new one if it recovers
      vTaskDelay(pdMS_TO_TICKS(100));  // Give time for logs to print
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

  /*
  std::string motd_req = "MOTD" + END_TOKEN;
  this->client_sock->send(motd_req.c_str(), motd_req.length(), 0);
  waitForClientData();
  ESP_LOGD(TAG, "Core [%d] - MOTD: %s", this->core, this->client_buffer.c_str());
  */
}

void MiningJob::waitForClientData() {
  this->client_buffer = "";
  char c;
  uint32_t stopWatch = millis();

  if (!this->config->is_ready)
    return;
  if (!this->is_connected || !this->client_sock)
    return;

  while (this->is_connected && this->client_sock) {
    int16_t res = this->client_sock->read(&c, 1);

    if (res > 0) {
      if (c == END_TOKEN) {
        if (this->client_buffer.length() == 0) {
          this->client_buffer = "???\n";  // NOTE: Should never happen
        }
        break;
      }
      this->client_buffer += c;
    } else if (res == 0) {
      ESP_LOGW(TAG, "Core [%d] - Socket disconnected by pool while waiting for data.", this->core);
      this->is_connected = false;
      this->client_sock.reset();
      this->config->is_ready = false;
      break;
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        ESP_LOGE(TAG, "Core [%d] - Socket read error (errno: %d).", this->core, errno);
        this->is_connected = false;
        this->client_sock.reset();
        this->config->is_ready = false;
        break;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1));

    if (millis() - stopWatch > 120000) {
      ESP_LOGE(TAG, "Core [%d] - Timeout after 120s. Invalidating connection...", this->core);
      this->is_connected = false;
      this->client_sock.reset();
      this->config->is_ready = false;
      break;
    }
  }
}

void MiningJob::submit(uint32_t counter, uint32_t hashrate, float elapsed_time_s) {
  if (!this->config->is_ready)
    return;
  if (!this->is_connected || !this->client_sock)
    return;

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

  int16_t sent_bytes = this->client_sock->send(reply.c_str(), reply.length(), 0);

  if (sent_bytes < 0) {
    ESP_LOGE(TAG, "Core [%d] - Failed to send share to pool.", this->core);
    this->is_connected = false;
    this->client_sock.reset();
    this->config->is_ready = false;
    return;
  }

  uint32_t ping_start = millis();
  waitForClientData();
  this->ping = millis() - ping_start;

  bool is_good = (this->client_buffer.find("GOOD") != std::string::npos);
  if (is_good) {
    this->errors = 0;
    this->accepted_share_count++;
  } else {
    this->errors++;
  }

  if (is_good) {
    ESP_LOGI(TAG, "Core [%d] - %s share #%d (%lu) hashrate: %.2f kH/s (%.2fs) Ping: %lums (%s)", this->core,
             this->client_buffer.c_str(), this->share_count.load(), counter, hashrate / 1000.0f, elapsed_time_s,
             this->ping.load(), this->config->node_id.c_str());
  } else {
    ESP_LOGW(TAG, "Core [%d] - %s share #%d (%lu) hashrate: %.2f kH/s (%.2fs) Ping: %lums (%s)", this->core,
             this->client_buffer.c_str(), this->share_count.load(), counter, hashrate / 1000.0f, elapsed_time_s,
             this->ping.load(), this->config->node_id.c_str());
  }
  this->client_buffer.clear();
}

void MiningJob::askForJob() {
  if (!this->config->is_ready)
    return;
  if (!this->is_connected || !this->client_sock)
    return;

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

  int16_t sent_bytes = this->client_sock->send(job_req.c_str(), job_req.length(), 0);

  if (sent_bytes < 0) {
    ESP_LOGE(TAG, "Core [%d] - Failed to send job request to pool.", this->core);
    this->is_connected = false;
    this->client_sock.reset();
    this->config->is_ready = false;
    return;
  }

  waitForClientData();

  ESP_LOGD(TAG, "Core [%d] - Received job with size of %d bytes: %s", this->core, this->client_buffer.length(),
           this->client_buffer.c_str());

  if (parse()) {
    ESP_LOGD(TAG, "Core [%d] - Parsed job: %s %s %lu", this->core, this->last_block_hash.c_str(),
             this->expected_hash_str.c_str(), this->difficulty.load());
  } else {
    this->errors++;
    ESP_LOGE(TAG, "Core [%d] - Job parsing failed!", this->core);
  }
}

}  // namespace esphome::duco
