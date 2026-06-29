## Duco Miner Component

!!! warning
    ESP8266 supported but not tested.

!!! example annotate "Duco Miner Component"

    ``` { .yaml .copy .annotate }
    external_components:
      - source:
          type: git
          url: https://github.com/andrewjswan/duco-miner
          ref: main
        components: [duco]

    duco:
      id: miner
      username: "${duco_user}" (1)
      key: "${duco_key}" (2)
      name: "${worker_name}" (3)
    ```
1. Username
2. Mining key
3. Miner name (leave blank for default)

!!! note "Mining key"
    Please note that the `mining key` is **not** your `password`. It's a separate passphrase that you set during registration (or set in the wallet).

!!! tip "Using Secrets"
    Username, Key can be substituted using `Secrets` file `secrets.yaml`.

    !!! example
        ``` { .yaml .copy .annotate }
        duco:
          username: !secret my_duco_username
          key: !secret my_duco_mining_key
        ```

!!! danger
    In order to keep your secrets safe, the `secrets.yaml` file should NOT be checked into git or any other version control system.

### Parameters reference

- **id** (optional, ID): Manually specify the identifier used for code generation and in service definitions.

- **username** (required, string): Duino-Coin username.

- **key** (required, string): Duino-Coin mining key.

- **name** (optional, string): Custom miner name / identifier. Defaults to `Auto`.

- **temperature** (optional, ID): Temperature sensor ID for transmission to the [Duino IoT](https://github.com/duino-coin/duino-coin/wiki/Duino's-take-on-the-Internet-of-Things).

- **humidity** (optional, ID): Humidity Sensor ID for transmission to the [Duino IoT](https://github.com/duino-coin/duino-coin/wiki/Duino's-take-on-the-Internet-of-Things).

- **cpu_temperature** (optional, ID): CPU Temperature sensor ID for transmission to the [Duino IoT](https://github.com/duino-coin/duino-coin/wiki/Duino's-take-on-the-Internet-of-Things).

- **esphome** (optional, boolean): Add ESPHome version for transmission to the [Duino IoT](https://github.com/duino-coin/duino-coin/wiki/Duino's-take-on-the-Internet-of-Things). Defaults to `false`.

- **enable_mimicry** (optional, boolean): Identify yourself to the server as an official client :material-information-outline:{ title="Like: Official ESP32 Miner" }. Defaults to `false`.

## Duco Sensors

!!! example annotate "Duco Miner Sensors"
    ``` { .yaml .copy .annotate }
    sensor:
      - platform: duco
        hashrate:
          name: "Hashrate"
        accepted_shares:
          name: "Accepted shares"
        total_shares:
          name: "Total shares"
        difficulty:
          name: "Difficulty"
        accept_rate:
          name: "Accept rate"
        share_rate:
          name: "Share rate"
        ping:
          name: "Ping"
    ```

!!! example annotate "Duco Miner Binnary Sensors"
    ``` { .yaml .copy .annotate }
    binary_sensor:
      - platform: duco
        status:
          name: "Miner status"
    ```

!!! example annotate "Duco Miner Text Sensors"
    ``` { .yaml .copy .annotate }
    text_sensor:
      - platform: duco
        cores_status:
          name: "Cores status"
        pool:
          name: "Current Pool"
    ```

!!! tip "Cores status"
    * **~** - Task problem :material-information-outline:{ title="Only for ESP32" }
    * **-** - Job problem
    * **X** - A significant number of errors in the job tasks
    * **\*** - No problem

*[ESP32]: A series of `system-on-chip` microcontrollers from the Chinese manufacturer **Espressif**, featuring integrated Wi-Fi and Bluetooth controllers, low power consumption, and an affordable price.
*[ESP8266]: A microcontroller from the Chinese manufacturer **Espressif** with a Wi-Fi interface. For example, the NodeMCU or Wemos D1 Mini.
*[ID]: Quite an important aspect of ESPHome are `IDs`. They are used to connect components from different domains.
*[boolean]: `true` or `false`
*[string]: Strings are enclosed in double quotes (") or single quotes (').
