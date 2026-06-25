#include "counter.h"
#include "duco.h"
#include "mining.h"

#include "esphome/components/socket/socket.h"
#include "esphome/core/application.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#include <algorithm>

namespace esphome::duco {

// clang-format off
constexpr uint8_t base36CharValues[75] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, 0,                                                                        // 0 to 9
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 0, 0, 0, 0, 0, 0, // Upper case letters
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35                    // Lower case letters
};
// clang-format on

#define SPC_TOKEN ' '
#define END_TOKEN '\n'
#define SEP_TOKEN ','
#define IOT_TOKEN '@'

MiningJob::MiningJob(uint8_t core, MiningConfig *config, Duco *parent) {
  this->core = core;
  this->config = config;
  this->parent = parent;
  this->client_buffer = "";
  dsha1 = new DSHA1();
  dsha1->warmup();
}

void MiningJob::mine() {
  handleSystemEvents();
  if (!this->config->is_ready)
    return;

  connectToNode();
  if (!this->is_connected)
    return;

  askForJob();
  if (this->client_buffer.empty() || this->client_buffer == "???\n")
    return;

  dsha1->reset().write((const unsigned char *) this->last_block_hash.c_str(), this->last_block_hash.length());

  uint32_t start_time = micros();
  for (Counter<10> counter; counter <= this->difficulty; ++counter) {
    DSHA1 ctx = *dsha1;
    ctx.write((const unsigned char *) counter.c_str(), counter.strlen()).finalize(this->hashArray);

    if (counter % 8000 == 0) {
      handleSystemEvents();
    }
    if (!this->config->is_ready) {
      handleSystemEvents();
      return;
    }

    if (memcmp(this->expected_hash, this->hashArray, 20) == 0) {
      uint32_t elapsed_micros = micros() - start_time;
      float elapsed_time_s = elapsed_micros * 0.000001f;
      this->share_count++;

      uint32_t current_hashrate = 0;
      if (elapsed_time_s > 0.0f) {
        current_hashrate = counter / elapsed_time_s;
      } else {
        // Fallback for instant shares to prevent divide-by-zero or fake crazy values
        current_hashrate = counter;
      }
      this->hashrate = current_hashrate;
      submit(counter, current_hashrate, elapsed_time_s);

      if (this->parent != nullptr) {
        this->parent->on_share_found_callback();
      }

      break;  // Share found, exit loop to request the next job
    }
  }
  ESP_LOGD(TAG, "Core [%d] - Mine complete.", this->core);
}

uint8_t *MiningJob::hexStringToUint8Array(const std::string &hexString, uint8_t *uint8Array,
                                          const uint32_t arrayLength) {
  if (hexString.length() < arrayLength * 2) {
    return uint8Array;
  }

  esphome::parse_hex(hexString, uint8Array, arrayLength);
  return uint8Array;

  const char *hexChars = hexString.c_str();
  for (uint32_t i = 0; i < arrayLength; ++i) {
    uint8_t high = base36CharValues[hexChars[i * 2] - '0'];
    uint8_t low = base36CharValues[hexChars[i * 2 + 1] - '0'];
    uint8Array[i] = (high << 4) + low;
  }
  return uint8Array;
}

bool MiningJob::parse() {
  if (this->client_buffer.empty() || this->client_buffer == "???\n") {
    ESP_LOGE(TAG, "Core [%d] - Cannot parse empty or invalid client buffer", this->core);
    return false;
  }

  this->client_buffer.erase(std::remove(this->client_buffer.begin(), this->client_buffer.end(), '\r'),
                            this->client_buffer.end());
  this->client_buffer.erase(std::remove(this->client_buffer.begin(), this->client_buffer.end(), '\n'),
                            this->client_buffer.end());

  std::vector<std::string> tokens;
  size_t start = 0;
  size_t end = this->client_buffer.find(SEP_TOKEN);

  while (end != std::string::npos) {
    tokens.push_back(this->client_buffer.substr(start, end - start));
    start = end + 1;
    end = this->client_buffer.find(SEP_TOKEN, start);
  }
  tokens.push_back(this->client_buffer.substr(start));

  // 3 token: last_hash, expected_hash, difficulty
  if (tokens.size() < 3) {
    ESP_LOGE(TAG, "Core [%d] - Parsing failed. Expected 3 tokens, got %d. Buffer: %s", this->core, tokens.size(),
             this->client_buffer.c_str());
    return false;
  }

  this->last_block_hash = tokens[0];
  this->expected_hash_str = tokens[1];

  hexStringToUint8Array(this->expected_hash_str, this->expected_hash, 20);

  char *endptr;
  uint32_t parsed_diff = std::strtoul(tokens[2].c_str(), &endptr, 10);
  if (endptr == tokens[2].c_str()) {
    ESP_LOGE(TAG, "Core [%d] - Difficulty token '%s' is not a valid number", this->core, tokens[2].c_str());
    return false;
  }
  this->difficulty = (static_cast<uint32_t>(parsed_diff) * 100);
  return true;
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
