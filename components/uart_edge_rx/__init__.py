"""Edge-timed receive for ESP8266 software UART buses.

ESPHome's ESP8266 software UART samples every bit of a received byte inside
the pin interrupt with interrupts masked, about 1 ms per byte at 9600 baud.
Under sustained traffic that starves the Wi-Fi driver. This component
replaces the receive interrupt of the referenced bus with one that only
records edge times, and puts the decoded bytes into the bus's own buffer.

The bus stays a normal `uart:` bus. Code is generated only on the ESP8266;
at runtime the component steps aside when the bus got a hardware UART, so
it is safe to add for any bus.
"""

import logging

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import uart
from esphome.const import CONF_BAUD_RATE, CONF_ID, CONF_RX_PIN, CONF_UART_ID
from esphome.core import CORE

_LOGGER = logging.getLogger(__name__)

DEPENDENCIES = ["uart"]
MULTI_CONF = True

DOMAIN = "uart_edge_rx"
# Below this, the decoder's interval clamp holds less than a frame. Above
# this, interrupt latency comes too close to half a bit time.
MIN_BAUD_RATE = 300
MAX_BAUD_RATE = 38400

uart_edge_rx_ns = cg.esphome_ns.namespace("uart_edge_rx")
UartEdgeRx = uart_edge_rx_ns.class_("UartEdgeRx", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(UartEdgeRx),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    }
).extend(cv.COMPONENT_SCHEMA)


def _bus_config(full_config, bus_id):
    for bus in full_config.get("uart", []):
        if bus[CONF_ID].id == bus_id.id:
            return bus
    return None


def _final_validate(config):
    full_config = fv.full_config.get()
    bus_id = config[CONF_UART_ID]
    bus = _bus_config(full_config, bus_id)
    if bus is None:
        raise cv.Invalid(f"'{bus_id.id}' is not a uart: bus", path=[CONF_UART_ID])
    baud_rate = bus[CONF_BAUD_RATE]
    if not MIN_BAUD_RATE <= baud_rate <= MAX_BAUD_RATE:
        raise cv.Invalid(
            f"bus '{bus_id.id}' runs at {baud_rate} baud; edge-timed receive "
            f"supports {MIN_BAUD_RATE} to {MAX_BAUD_RATE}",
            path=[CONF_UART_ID],
        )
    same_bus = [
        entry
        for entry in full_config.get(DOMAIN, [])
        if entry[CONF_UART_ID].id == bus_id.id
    ]
    if len(same_bus) > 1:
        raise cv.Invalid(
            f"bus '{bus_id.id}' is listed more than once", path=[CONF_UART_ID]
        )
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    if not CORE.is_esp8266:
        _LOGGER.info("%s: only the ESP8266 has a software UART; nothing to do", DOMAIN)
        return
    bus = _bus_config(CORE.config, config[CONF_UART_ID])
    if CONF_RX_PIN not in bus:
        _LOGGER.info(
            "%s: bus '%s' has no RX pin; nothing to do", DOMAIN, bus[CONF_ID].id
        )
        return

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_UART_ID])
    cg.add(var.set_uart(parent))
