import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_STATUS,
    ENTITY_CATEGORY_DIAGNOSTIC,
    DEVICE_CLASS_CONNECTIVITY,
)
from .const import ICON_MINECRAFT

from . import DucoComponent

DEPENDENCIES = ["duco"]


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(DucoComponent),
        cv.Optional(CONF_STATUS): binary_sensor.binary_sensor_schema(
            icon = ICON_MINECRAFT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            device_class=DEVICE_CLASS_CONNECTIVITY,
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_ID])

    if CONF_STATUS in config:
        sens = await  binary_sensor.new_binary_sensor(config[CONF_STATUS])
        cg.add(parent.set_status(sens))
