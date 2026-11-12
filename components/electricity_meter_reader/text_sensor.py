import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import text_sensor, uart
from esphome.const import CONF_TRIGGER_ID

DEPENDENCIES = ["uart"]

electricity_meter_reader_ns = cg.esphome_ns.namespace("electricity_meter_reader")
ElectricityMeterReader = electricity_meter_reader_ns.class_(
    "ElectricityMeterReader", text_sensor.TextSensor, cg.Component, uart.UARTDevice
)
TelegramDroppedTrigger = electricity_meter_reader_ns.class_(
    "TelegramDroppedTrigger", automation.Trigger.template(cg.uint32)
)

# Runs for every dropped SML telegram with `lines_lost`, the lines it would
# have carried.
CONF_ON_TELEGRAM_DROPPED = "on_telegram_dropped"

CONFIG_SCHEMA = (
    text_sensor.text_sensor_schema(ElectricityMeterReader)
    .extend(
        {
            cv.Optional(CONF_ON_TELEGRAM_DROPPED): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(TelegramDroppedTrigger)}
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    for conf in config.get(CONF_ON_TELEGRAM_DROPPED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint32, "lines_lost")], conf)
