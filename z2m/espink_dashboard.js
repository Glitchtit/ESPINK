/* Zigbee2MQTT external converter for the ESPINK e-paper dashboard.
 *
 * Install: copy to <z2m-data>/external_converters/espink_dashboard.js and restart
 * Z2M (Settings -> check the log for "loaded external converter").
 * Requires Z2M 2.x (modern modernExtend / DefinitionWithExtend API).
 *
 * Firmware contract (must match main/slots.h + the Phase 1 Zigbee handler):
 *   - plain custom cluster 0xFC00 (no manufacturer code on either side)
 *   - one CHAR_STR attribute per text slot (<= 60 chars; no APS fragmentation)
 *   - alert_active is a BOOLEAN attribute (HA switch); the firmware maps it to
 *     the "0"/"1" alert slot string
 *   - command 0x00 "render" forces an immediate repaint
 * Each text/binary is access:'ALL' so Home Assistant can WRITE it. */

const {Zcl} = require('zigbee-herdsman');
const m = require('zigbee-herdsman-converters/lib/modernExtend');

const CLUSTER = 'espinkDashboard';
/* The firmware registers cluster 0xFC00 as a PLAIN custom cluster (no
 * manufacturer code), so writes here must NOT carry one either. If writes ever
 * fail with "no matching cluster/attribute", add a manufacturerCode on BOTH the
 * device attributes and here (manufacturerCode + ID on deviceAddCustomCluster). */
const opts = {disableDefaultResponse: true};

/* Helper: one writable text slot -> HA `text` entity. */
const slot = (name, attribute, description) =>
    m.text({name, cluster: CLUSTER, attribute, description,
            access: 'ALL', zigbeeCommandOptions: opts, entityCategory: 'config'});

const definition = {
    zigbeeModel: ['ESPINK-7in5b'],
    model: 'ESPINK-7in5b',
    vendor: 'ESPINK',
    description: 'Zigbee e-paper dashboard (ESP32-C6, Waveshare 7.5" B)',
    extend: [
        m.deviceAddCustomCluster(CLUSTER, {
            ID: 0xfc00,
            attributes: {
                hdrLeft:     {ID: 0x0000, type: Zcl.DataType.CHAR_STR},
                hdrRight:    {ID: 0x0001, type: Zcl.DataType.CHAR_STR},
                nowValue:    {ID: 0x0010, type: Zcl.DataType.CHAR_STR},
                nowLabel:    {ID: 0x0011, type: Zcl.DataType.CHAR_STR},
                fc1:         {ID: 0x0020, type: Zcl.DataType.CHAR_STR},
                fc2:         {ID: 0x0021, type: Zcl.DataType.CHAR_STR},
                fc3:         {ID: 0x0022, type: Zcl.DataType.CHAR_STR},
                cal1:        {ID: 0x0030, type: Zcl.DataType.CHAR_STR},
                cal2:        {ID: 0x0031, type: Zcl.DataType.CHAR_STR},
                cal3:        {ID: 0x0032, type: Zcl.DataType.CHAR_STR},
                cal4:        {ID: 0x0033, type: Zcl.DataType.CHAR_STR},
                sns1:        {ID: 0x0040, type: Zcl.DataType.CHAR_STR},
                sns2:        {ID: 0x0041, type: Zcl.DataType.CHAR_STR},
                sns3:        {ID: 0x0042, type: Zcl.DataType.CHAR_STR},
                sns4:        {ID: 0x0043, type: Zcl.DataType.CHAR_STR},
                alertText:   {ID: 0x0050, type: Zcl.DataType.CHAR_STR},
                alertActive: {ID: 0x0051, type: Zcl.DataType.BOOLEAN},
            },
            commands: {
                render: {ID: 0x00, parameters: []},
            },
            commandsResponse: {},
        }),
        slot('hdr_left',  'hdrLeft',  'Header left (date)'),
        slot('hdr_right', 'hdrRight', 'Header right (status/updated time)'),
        slot('now_value', 'nowValue', 'Big value, e.g. "23°"'),
        slot('now_label', 'nowLabel', 'Condition, "icon:text" e.g. "sun:Partly cloudy"'),
        slot('fc1', 'fc1', 'Forecast 1, "day:icon:hi/lo"'),
        slot('fc2', 'fc2', 'Forecast 2'),
        slot('fc3', 'fc3', 'Forecast 3'),
        slot('cal1', 'cal1', 'Agenda line 1'),
        slot('cal2', 'cal2', 'Agenda line 2'),
        slot('cal3', 'cal3', 'Agenda line 3'),
        slot('cal4', 'cal4', 'Agenda line 4'),
        slot('sns1', 'sns1', 'Sensor tile 1'),
        slot('sns2', 'sns2', 'Sensor tile 2'),
        slot('sns3', 'sns3', 'Sensor tile 3'),
        slot('sns4', 'sns4', 'Sensor tile 4'),
        slot('alert_text', 'alertText', 'Red alert ribbon text'),
        m.binary({name: 'alert_active', cluster: CLUSTER, attribute: 'alertActive',
                  valueOn: ['ON', 1], valueOff: ['OFF', 0], access: 'ALL',
                  description: 'Show the red alert ribbon', zigbeeCommandOptions: opts,
                  entityCategory: 'config'}),
        /* Battery percentage (Phase 2) comes from the standard Power
         * Configuration cluster and needs no custom converter; add
         * m.battery() here once the firmware reports it. */
    ],
};

module.exports = definition;
