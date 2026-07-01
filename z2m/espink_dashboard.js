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
/* Plain custom cluster (no manufacturer code). Each attribute below MUST carry
 * `write: true` (and `read: true`) or herdsman's processAttributeWrite rejects
 * the set client-side with "NOT_AUTHORIZED ... is not writable" before anything
 * reaches the device. Adding a manufacturerCode here is unnecessary and breaks
 * Z2M's cluster name-resolution ("has no input cluster"), so we omit it. */
const opts = {disableDefaultResponse: true};

/* The modernExtend text() convertSet calls `validate(value)` unconditionally
 * (no default), so every m.text MUST supply a validate function or Z2M throws
 * "TypeError: validate is not a function" before transmitting. We also use it to
 * enforce the firmware's per-slot limit (<= 60 chars; no APS fragmentation). */
const validateSlot = (value) => {
    if (typeof value !== 'string') throw new Error('ESPINK slot value must be a string');
    if (value.length > 60) throw new Error('ESPINK slot value too long (max 60 chars)');
};

/* Helper: one writable text slot -> HA `text` entity. */
const slot = (name, attribute, description) =>
    m.text({name, cluster: CLUSTER, attribute, description, access: 'ALL',
            zigbeeCommandOptions: opts, entityCategory: 'config', validate: validateSlot});

const definition = {
    zigbeeModel: ['ESPINK-7in5b'],
    model: 'ESPINK-7in5b',
    vendor: 'ESPINK',
    description: 'Zigbee e-paper dashboard (ESP32-C6, Waveshare 7.5" B)',
    extend: [
        m.deviceAddCustomCluster(CLUSTER, {
            ID: 0xfc00,
            attributes: {
                // `write: true` + `read: true` are REQUIRED — herdsman rejects
                // the set client-side otherwise ("... is not writable").
                hdrLeft:     {ID: 0x0000, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                hdrRight:    {ID: 0x0001, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                nowValue:    {ID: 0x0010, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                nowLabel:    {ID: 0x0011, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                nowHilo:     {ID: 0x0012, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                fc1:         {ID: 0x0020, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                fc2:         {ID: 0x0021, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                fc3:         {ID: 0x0022, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                cal1:        {ID: 0x0030, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                cal2:        {ID: 0x0031, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                cal3:        {ID: 0x0032, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                cal4:        {ID: 0x0033, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                sns1:        {ID: 0x0040, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                sns2:        {ID: 0x0041, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                sns3:        {ID: 0x0042, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                sns4:        {ID: 0x0043, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                alertText:   {ID: 0x0050, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                alertActive: {ID: 0x0051, type: Zcl.DataType.BOOLEAN,  read: true, write: true},
                headache:    {ID: 0x0070, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                shop1:       {ID: 0x0080, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                shop2:       {ID: 0x0081, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                shop3:       {ID: 0x0082, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                shop4:       {ID: 0x0083, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                shop5:       {ID: 0x0084, type: Zcl.DataType.CHAR_STR, read: true, write: true},
                shop6:       {ID: 0x0085, type: Zcl.DataType.CHAR_STR, read: true, write: true},
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
        slot('now_hilo', 'nowHilo', 'Today high/low "HH/LL" (high >=25 shows red)'),
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
        slot('headache', 'headache', 'Headache index 0-100 (drives the severity icon)'),
        slot('shop1', 'shop1', 'Shopping list line 1'),
        slot('shop2', 'shop2', 'Shopping list line 2'),
        slot('shop3', 'shop3', 'Shopping list line 3'),
        slot('shop4', 'shop4', 'Shopping list line 4'),
        slot('shop5', 'shop5', 'Shopping list line 5'),
        slot('shop6', 'shop6', 'Shopping list line 6 ("And X more items" if truncated)'),
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
