# NMEA2000 PGN Decoder for ESP-IDF

## Overview

This system decodes NMEA2000 (CAN) messages received on your ESP32 and displays them in human-readable format on the terminal.

## How It Works

### 1. **JSON PGN Database** (`main/PGNS/NMEA_database_1_300.json`)
   - Converted from the XML PGN definition file using `convert_pgn_xml_to_json.py`
   - Contains all PGN definitions with field metadata (bit offsets, resolutions, units, etc.)
   - Stored on SD card for easy updates without recompiling

### 2. **PGN Parser Component** (`components/pgn_parser/`)
   - `pgn_json_parser.h/c` - Core parsing library using cJSON (ESP-IDF built-in)
   - Functions:
     - `pgn_json_load()` - Load JSON database from SD card
     - `pgn_get_definition()` - Lookup PGN by number
     - `pgn_get_definition_by_id()` - Lookup PGN by ID string
     - `pgn_print_all_ids()` - List all available PGNs

### 3. **CAN Message Decoder** (`main/user/can/can.c`)
   - Enhanced with PGN decoding logic
   - On startup:
     - Loads PGN database from `/sdcard/PGNS/NMEA_database_1_300.json`
     - Lists all available PGNs to terminal
   - For each incoming CAN message:
     - Extracts PGN number from CAN identifier
     - Looks up PGN definition
     - Decodes all fields with proper bit extraction and resolution scaling
     - Outputs to terminal with field names, values, and units

## NMEA2000 CAN ID Structure

The 29-bit CAN identifier encodes:
```
Bits 28-26: Priority
Bit 25:     Reserved  
Bit 24:     Data Page
Bits 23-16: PDU Format
Bits 15-8:  PDU Specific
Bits 7-0:   Source Address
```

PGN is calculated from PDU Format and PDU Specific:
- If PDU Format < 240: `PGN = (PDU Format << 8)`
- If PDU Format >= 240: `PGN = (PDU Format << 8) | PDU Specific`

## Terminal Output Example

```
I (12345) CAN_DECODER: === PGN MESSAGE ===
I (12345) CAN_DECODER: PGN: 126992 | Name: systemTime
I (12345) CAN_DECODER: Description: System Time
I (12345) CAN_DECODER: Priority: 3 | Source: 5 | Length: 8 bytes
I (12345) CAN_DECODER: Fields:
I (12345) CAN_DECODER:   SID (sid): 0.0000
I (12345) CAN_DECODER:   Source (source): 3.0000
I (12345) CAN_DECODER:   Date (date): 19000.0000d
I (12345) CAN_DECODER:   Time (time): 43200.0000s
```

## Field Decoding

Each field is decoded using:
1. **Bit extraction**: Uses `BitOffset` and `BitLength` to extract bits from raw data
2. **Resolution scaling**: Multiplies raw value by `Resolution` factor
3. **Unit application**: Displays with the field's `Unit` (if available)

The decoder automatically:
- Skips reserved fields
- Handles different bit widths and byte ordering
- Applies scaling factors for proper value representation

## Adding Custom Decoding

To add custom processing for specific PGNs, modify `can.c`:

```c
// In can_task(), after loading pgn_database:
cJSON *wind_pgn = pgn_get_definition_by_id(pgn_database, "windData");
if (wind_pgn) {
    // Process wind PGN specially
}
```

## Dependencies

- **cJSON** - JSON parsing (built-in to ESP-IDF, no external dependency needed)
- **NMEA2000 PGN XML database** - For generating the JSON file

## File Structure

```
/Users/david/Development_Local/ESP-IDF/NMEA2000Instruments/
├── convert_pgn_xml_to_json.py          # Python converter script
├── main/PGNS/
│   ├── NMEA_database_1_300.xml         # Original XML file
│   └── NMEA_database_1_300.json        # Converted JSON (on SD card)
├── main/user/can/can.c                 # Enhanced CAN decoder
└── components/pgn_parser/
    ├── pgn_json_parser.h
    ├── pgn_json_parser.c
    ├── pgn_example.c
    └── CMakeLists.txt
```

## Troubleshooting

**"Failed to load PGN database"**
- Ensure `/sdcard/PGNS/NMEA_database_1_300.json` exists on SD card
- Check SD card is mounted correctly
- Check file permissions

**Unknown PGNs showing as raw hex**
- The PGN might not be in the database
- Check `pgn_print_all_ids()` output to see available PGNs
- Consider updating the XML source database

**Incorrect field values**
- Verify `BitOffset` and `BitLength` in JSON match NMEA2000 standard
- Check if special LOOKUP or STRING fields need custom decoding
- Ensure CAN baud rate is 250 kbit/s or 500 kbit/s
