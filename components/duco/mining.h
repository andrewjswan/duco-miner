#pragma once
#pragma GCC optimize("-Ofast")

#include "dsha1.h"

#include "esphome/components/socket/socket.h"

#include <cstdint>
#include <string>
#include <atomic>
#include <memory>

#if defined(USE_ESP8266)
#include <ESP8266WiFi.h>
#endif

namespace esphome::duco {

inline constexpr uint8_t ERROR_THRESHOLD = 55;

inline constexpr char SPC_TOKEN = ' ';
inline constexpr char END_TOKEN = '\n';
inline constexpr char SEP_TOKEN = ',';
inline constexpr char IOT_TOKEN = '@';

struct MiningConfig;
class Duco;

class MiningJob {
 public:
  MiningJob(uint8_t core, MiningConfig *config, Duco *parent);
  ~MiningJob() { delete this->dsha1; }

  void mine();
  bool problem() const;

#if defined(USE_ESP32)
  std::atomic<uint32_t> hashrate{0};
  std::atomic<uint32_t> difficulty{0};
  std::atomic<uint32_t> share_count{0};
  std::atomic<uint32_t> accepted_share_count{0};
  std::atomic<uint32_t> ping{0};
#elif defined(USE_ESP8266)
  uint32_t hashrate{0};
  uint32_t difficulty{0};
  uint32_t share_count{0};
  uint32_t accepted_share_count{0};
  uint32_t ping{0};
#endif

 private:
  MiningConfig *config;
  Duco *parent;

  uint8_t core = 0;

  DSHA1 *dsha1;

  std::string client_buffer{};
  std::string last_block_hash{};
  std::string expected_hash_str{};
  uint8_t hashArray[20];
  uint8_t expected_hash[20];

#if defined(USE_ESP32)
  std::unique_ptr<esphome::socket::Socket> client_sock{nullptr};
#elif defined(USE_ESP8266)
  WiFiClient client_sock;
#endif

  bool is_connected{false};
#if defined(USE_ESP32)
  std::atomic<uint32_t> errors{0};
#elif defined(USE_ESP8266)
  uint32_t errors{0};
#endif

  void handleSystemEvents(void);

  uint8_t *hex_string_to_uint8_array(const std::string &hexString, uint8_t *uint8Array, const uint32_t arrayLength);

  void connectToNode();
  void waitForClientData();
  void submit(uint32_t counter, uint32_t hashrate, float elapsed_time_s);
  bool parse();
  void askForJob();
};

}  // namespace esphome::duco
