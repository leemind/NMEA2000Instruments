#!/usr/bin/env python3
"""
Convert NMEA 2000 PGN XML database to JSON format for ESP-IDF
"""

import xml.etree.ElementTree as ET
import json
import sys
from pathlib import Path

def parse_field(field_elem):
    """Extract field data from XML element"""
    field = {}
    
    # Extract all child elements
    for child in field_elem:
        tag = child.tag
        text = child.text
        
        if text is None:
            continue
            
        # Convert to appropriate types
        if tag in ['Order', 'BitLength', 'BitOffset', 'BitStart', 'RangeMin', 'RangeMax']:
            try:
                field[tag] = int(text)
            except ValueError:
                field[tag] = text
        elif tag in ['Resolution']:
            try:
                field[tag] = float(text)
            except ValueError:
                field[tag] = text
        elif tag in ['Signed']:
            field[tag] = text.lower() == 'true'
        else:
            field[tag] = text
    
    return field

def parse_pgn(pgn_elem):
    """Extract PGN data from XML element"""
    pgn = {}
    
    for child in pgn_elem:
        tag = child.tag
        text = child.text
        
        if tag == 'Fields':
            # Handle Fields separately
            fields = []
            for field_elem in child.findall('Field'):
                fields.append(parse_field(field_elem))
            pgn['Fields'] = fields
        elif text is not None:
            # Convert to appropriate types
            if tag in ['PGN', 'Priority', 'FieldCount', 'Length']:
                try:
                    pgn[tag] = int(text)
                except ValueError:
                    pgn[tag] = text
            elif tag in ['Complete']:
                pgn[tag] = text.lower() == 'true'
            else:
                pgn[tag] = text
    
    return pgn

def convert_xml_to_json(xml_file, output_file):
    """Convert XML PGN database to JSON format"""
    
    try:
        tree = ET.parse(xml_file)
        root = tree.getroot()
    except ET.ParseError as e:
        print(f"Error parsing XML: {e}", file=sys.stderr)
        return False
    
    pgns = []
    pgn_infos = root.findall('.//PGNInfo')
    
    print(f"Found {len(pgn_infos)} PGN definitions...")
    
    for pgn_elem in pgn_infos:
        pgn = parse_pgn(pgn_elem)
        if pgn:
            pgns.append(pgn)
    
    # Create output structure
    output_data = {
        "version": "1.300",
        "pgns": pgns
    }
    
    # Write JSON file with nice formatting
    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(output_data, f, indent=2, ensure_ascii=False)
        print(f"Successfully converted {len(pgns)} PGNs to {output_file}")
        return True
    except IOError as e:
        print(f"Error writing output file: {e}", file=sys.stderr)
        return False

if __name__ == '__main__':
    # Default file paths
    script_dir = Path(__file__).parent
    xml_file = script_dir / 'main' / 'PGNS' / 'NMEA_database_1_300.xml'
    output_file = script_dir / 'main' / 'PGNS' / 'NMEA_database_1_300.json'
    
    # Allow override via command line
    if len(sys.argv) > 1:
        xml_file = Path(sys.argv[1])
    if len(sys.argv) > 2:
        output_file = Path(sys.argv[2])
    
    if not xml_file.exists():
        print(f"Error: XML file not found: {xml_file}", file=sys.stderr)
        sys.exit(1)
    
    success = convert_xml_to_json(xml_file, output_file)
    sys.exit(0 if success else 1)
