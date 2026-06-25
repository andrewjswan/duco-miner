#include "counter.h"
#include "duco.h"
#include "mining.h"

#include "esphome/components/socket/socket.h"
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
#if defined(USE_ESP32)
  for (Counter<10> counter; counter <= this->difficulty.load(); ++counter) {
#elif defined(USE_ESP8266)
  for (Counter<10> counter; counter <= this->difficulty; ++counter) {
#endif
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

uint8_t *MiningJob::hex_string_to_uint8_array(const std::string &hexString, uint8_t *uint8Array,
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

  hex_string_to_uint8_array(this->expected_hash_str, this->expected_hash, 20);

  char *endptr;
  uint32_t parsed_diff = std::strtoul(tokens[2].c_str(), &endptr, 10);
  if (endptr == tokens[2].c_str()) {
    ESP_LOGE(TAG, "Core [%d] - Difficulty token '%s' is not a valid number", this->core, tokens[2].c_str());
    return false;
  }
  this->difficulty = (static_cast<uint32_t>(parsed_diff) * 100);
  return true;
}

}  // namespace esphome::duco
