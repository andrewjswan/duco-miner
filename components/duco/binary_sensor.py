"""Duco binary sensors."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.config_helpers import filter_source_files_from_platform
from esphome.const import (
    CONF_ID,
    CONF_STATUS,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    PlatformFramework,
)

from . import DucoComponent
from .const import ICON_MINECRAFT

DEPENDENCIES = ["duco"]


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(DucoComponent),
        cv.Optional(CONF_STATUS): binary_sensor.binary_sensor_schema(
            icon=ICON_MINECRAFT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            device_class=DEVICE_CLASS_CONNECTIVITY,
        ),
    },
)


async def to_code(config) -> None:
    """Code generation entry point."""
    parent = await cg.get_variable(config[CONF_ID])

    if CONF_STATUS in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_STATUS])
        cg.add(parent.set_status(sens))


FILTER_SOURCE_FILES = filter_source_files_from_platform(
    {
        "duco_esp32.cpp": {
            PlatformFramework.ESP32_IDF,
            PlatformFramework.ESP32_ARDUINO,
        },
        "mining_esp32.cpp": {
            PlatformFramework.ESP32_IDF,
            PlatformFramework.ESP32_ARDUINO,
        },
        "duco_esp8266.cpp": {PlatformFramework.ESP8266_ARDUINO},
        "mining_esp8266.cpp": {PlatformFramework.ESP8266_ARDUINO},
    },
)
