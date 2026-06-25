"""Duco text sensors."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from esphome.const import (
    CONF_ID,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import FILTER_SOURCE_FILES, DucoComponent  # noqa: F401
from .const import (
    CONF_CORES_STATUS,
    CONF_POOL,
    ICON_CPU,
    ICON_WEB,
)

DEPENDENCIES = ["duco"]


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(DucoComponent),
        cv.Optional(CONF_POOL): text_sensor.text_sensor_schema(
            icon=ICON_WEB,
        ),
        cv.Optional(CONF_CORES_STATUS): text_sensor.text_sensor_schema(
            icon=ICON_CPU,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    },
)


async def to_code(config) -> None:
    """Code generation entry point."""
    parent = await cg.get_variable(config[CONF_ID])

    if CONF_POOL in config:
        sens = await text_sensor.new_text_sensor(config[CONF_POOL])
        cg.add(parent.set_pool(sens))

    if CONF_CORES_STATUS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_CORES_STATUS])
        cg.add(parent.set_cores_status(sens))
