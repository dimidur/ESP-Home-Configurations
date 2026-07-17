import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID, CONF_UART_ID, CONF_TRIGGER_ID
from esphome import pins
from esphome import automation

CONF_SCANNING_TIMEOUT = "scanning_timeout"
CONF_ON_SCAN = "on_scan"
CONF_ON_LONG_PRESS = "on_long_press"
CONF_ON_SHORT_PRESS = "on_short_press"
CONF_ON_START_SCAN = "on_start_scan"
CONF_ON_STOP_SCAN = "on_stop_scan"
CONF_SCAN_TRIGGER_PIN = "scan_trigger_pin"
CONF_LED_PIN = "led_pin"
CONF_SCANNER_TRIGGER_PIN = "scanner_trigger_pin"

qrcode2_uart_ns = cg.esphome_ns.namespace('qrcode2_uart')
QRCode2UARTComponent = qrcode2_uart_ns.class_('QRCode2UARTComponent', cg.Component, uart.UARTDevice)

# Actions
StartScanAction = qrcode2_uart_ns.class_('StartScanAction', automation.Action)
StopScanAction = qrcode2_uart_ns.class_('StopScanAction', automation.Action)
# GetDeviceInfoAction = qrcode2_uart_ns.class_('GetDeviceInfoAction', automation.Action)  # Commented out - preserved for reference
ResetScannerAction = qrcode2_uart_ns.class_('ResetScannerAction', automation.Action)
UpdateLongPressDurationAction = qrcode2_uart_ns.class_('UpdateLongPressDurationAction', automation.Action)

# Triggers
ScanTrigger = qrcode2_uart_ns.class_('ScanTrigger', automation.Trigger.template(cg.std_string))

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(QRCode2UARTComponent),
    cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    cv.Optional(CONF_SCANNING_TIMEOUT, default="20s"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_SCAN_TRIGGER_PIN): pins.gpio_input_pin_schema,
    cv.Optional(CONF_LED_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_SCANNER_TRIGGER_PIN): pins.gpio_output_pin_schema,
    cv.Optional(CONF_ON_SCAN): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ScanTrigger),
    }),
    cv.Optional(CONF_ON_LONG_PRESS): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ScanTrigger),
    }),
    cv.Optional(CONF_ON_SHORT_PRESS): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ScanTrigger),
    }),
    cv.Optional(CONF_ON_START_SCAN): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ScanTrigger),
    }),
    cv.Optional(CONF_ON_STOP_SCAN): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ScanTrigger),
    }),
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    cg.add(var.set_scanning_timeout(config[CONF_SCANNING_TIMEOUT]))
    
    if CONF_SCAN_TRIGGER_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_SCAN_TRIGGER_PIN])
        cg.add(var.set_trigger_pin(pin))
    
    if CONF_LED_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_LED_PIN])
        cg.add(var.set_led_pin(pin))
    
    if CONF_SCANNER_TRIGGER_PIN in config:
        pin = await cg.gpio_pin_expression(config[CONF_SCANNER_TRIGGER_PIN])
        cg.add(var.set_scanner_trigger_pin(pin))
    
    # Configuration is applied in the setup() method after scanner initialization
    
    for conf in config.get(CONF_ON_SCAN, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_scan_trigger(trigger))
        await automation.build_automation(trigger, [(cg.std_string, "scan_result")], conf)
        
    for conf in config.get(CONF_ON_LONG_PRESS, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_long_press_trigger(trigger))
        await automation.build_automation(trigger, [(cg.std_string, "press_type")], conf)
        
    for conf in config.get(CONF_ON_SHORT_PRESS, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_short_press_trigger(trigger))
        await automation.build_automation(trigger, [(cg.std_string, "press_type")], conf)
        
    for conf in config.get(CONF_ON_START_SCAN, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_start_scan_trigger(trigger))
        await automation.build_automation(trigger, [(cg.std_string, "event")], conf)
        
    for conf in config.get(CONF_ON_STOP_SCAN, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
        cg.add(var.add_stop_scan_trigger(trigger))
        await automation.build_automation(trigger, [(cg.std_string, "event")], conf)
    


# Start scan action
@automation.register_action('qrcode2_uart.start_scan', StartScanAction, cv.Schema({
    cv.GenerateID(): cv.use_id(QRCode2UARTComponent),
}), synchronous=True)  # play() calls start_scan() inline and returns; no deferral
async def qrcode2_uart_start_scan_to_code(config, action_id, template_args, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_args, parent)

# Stop scan action
@automation.register_action('qrcode2_uart.stop_scan', StopScanAction, cv.Schema({
    cv.GenerateID(): cv.use_id(QRCode2UARTComponent),
}), synchronous=True)  # play() calls stop_scan() inline and returns; no deferral
async def qrcode2_uart_stop_scan_to_code(config, action_id, template_args, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_args, parent)

# Device information and reset actions

# DEVICE INFO ACTION - PRESERVED FOR FUTURE REFERENCE
# This automation was used to trigger device info requests from YAML
# Commented out to simplify code but preserved for future use if needed

# @automation.register_action('qrcode2_uart.get_device_info', GetDeviceInfoAction, cv.Schema({
#     cv.GenerateID(): cv.use_id(QRCode2UARTComponent),
# }))
# async def qrcode2_uart_get_device_info_to_code(config, action_id, template_args, args):
#     parent = await cg.get_variable(config[CONF_ID])
#     return cg.new_Pvariable(action_id, template_args, parent)

@automation.register_action('qrcode2_uart.reset_scanner', ResetScannerAction, cv.Schema({
    cv.GenerateID(): cv.use_id(QRCode2UARTComponent),
}), synchronous=True)  # play() resets inline and returns; no deferral
async def qrcode2_uart_reset_scanner_to_code(config, action_id, template_args, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_args, parent)

@automation.register_action('qrcode2_uart.update_long_press_duration', UpdateLongPressDurationAction, cv.Schema({
    cv.GenerateID(): cv.use_id(QRCode2UARTComponent),
    cv.Required('duration_ms'): cv.templatable(cv.uint32_t),
}), synchronous=True)  # play() sets duration inline and returns; no deferral
async def qrcode2_uart_update_long_press_duration_to_code(config, action_id, template_args, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_args, parent)
    template_ = await cg.templatable(config['duration_ms'], args, cg.uint32)
    cg.add(var.set_duration_ms(template_))
    return var
