---
title: Firmware & Installation Guide
description: Launch your DUCO mining setup in a few clicks using fully prepared YAML configurations for ESP8266/ESP32.
---

<script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>
<script src="https://ajax.googleapis.com/ajax/libs/jquery/3.7.1/jquery.min.js"></script>
<script src="../js/installer.js"></script>

<style>
  .md-typeset h1 {
    display: none;
  }
</style>

<div style="float: right; width: 30%; margin-left: 10px;" markdown="1">

[![Made for ESPHome](img/made-for-esphome.svg){ loading=lazy style="width:100% }](https://esphome.io/guides/made_for_esphome.html)

[![ESPHome](https://www.openhomefoundation.org/badges/esphome.png){ loading=lazy style="width:100% }](https://www.openhomefoundation.org)

</div>

!!! info "ESP Web Tools"
    User friendly tools to manage `ESP8266` and `ESP32` devices in the browser:

    * Install &amp; update firmware.
    * Connect device to the `Wi-Fi` network.
    * Visit the device's hosted `web interface`.
    * Access logs and send terminal commands.
    * Add devices to [Home Assistant](https://www.home-assistant.io).

## Install DucoMiner <b><span id="version"></span></b>

<div class="esp-installer-page">
<div class="radios">
  <label>
    <input
      type="radio"
      name="duco-miner"
      class="device"
      id=""
      value=""
      checked
    />
    <img src="../img/duco-miner.png" alt="Duco Miner" />
    <span></span>
  </label>
</div>

<div class="button-row">
  <esp-web-install-button manifest="../manifest.json"></esp-web-install-button>
</div>

</div>

## Advanced Users

- The device is adoptable in the [ESPHome dashboard](https://my.home-assistant.io/redirect/supervisor_addon/?addon=5c53de3b_esphome&repository_url=https%3A%2F%2Fgithub.com%2Fesphome%2Fhome-assistant-addon)
- The YAML configuration for additional boards and chips is available on [GitHub](https://github.com/andrewjswan/duco-miner)

---

[DucoMiner](https://github.com/andrewjswan/duco-miner) — Installer powered by [ESP Web Tools](https://esphome.github.io/esp-web-tools/).
