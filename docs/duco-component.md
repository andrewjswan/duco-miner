## Duco Miner Component

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

!!! warning
    ESP8266 supported but not tested.

### Parameters reference

- **id** (optional, ID): Manually specify the identifier used for code generation and in service definitions.

- **username** (required, string): Duino-Coin username.

- **key** (required, string): Duino-Coin mining key.

- **name** (optional, string): Custom miner name / identifier (leave blank for default).

- **temperature** (optional, ID): Temperature sensor ID for transmission to the [Duino IoT](https://github.com/duino-coin/duino-coin/wiki/Duino's-take-on-the-Internet-of-Things)

- **humidity** (optional, ID): Humidity Sensor ID for transmission to the [Duino IoT](https://github.com/duino-coin/duino-coin/wiki/Duino's-take-on-the-Internet-of-Things)

- **cpu_temperature** (optional, ID): CPU Temperature sensor ID for transmission to the [Duino IoT](https://github.com/duino-coin/duino-coin/wiki/Duino's-take-on-the-Internet-of-Things)

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
