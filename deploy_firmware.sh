#!/bin/bash

# Configuration
SERVER="firmware.sailstudio.tech"
REMOTE_PATH="/var/www/firmware.sailstudio.tech/WindInstruments/"
BIN_FILE="build/NMEA2000Instruments.bin"
VERSION_FILE="version.txt"

# Colors for output
GREEN='\033[0-32m'
RED='\033[0-31m'
NC='\033[0m' # No Color

echo "🚀 Starting firmware deployment..."

# Check if build file exists
if [ ! -f "$BIN_FILE" ]; then
    echo -e "${RED}Error: $BIN_FILE not found! Have you run 'idf.py build'?${NC}"
    exit 1
fi

# Check if version file exists
if [ ! -f "$VERSION_FILE" ]; then
    echo -e "${RED}Error: $VERSION_FILE not found in root directory!${NC}"
    exit 1
fi

CURRENT_VERSION=$(cat "$VERSION_FILE")
echo -e "${GREEN}Deploying Version: $CURRENT_VERSION${NC}"

# Upload files via scp
echo "Uploading to $SERVER..."
scp "$BIN_FILE" "$VERSION_FILE" "$SERVER:$REMOTE_PATH"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ Deployment successful!${NC}"
    echo "Firmware is now available at https://$SERVER/WindInstruments/NMEA2000Instruments.bin"
    echo "Version file is now available at https://$SERVER/WindInstruments/version.txt"
else
    echo -e "${RED}❌ Deployment failed. Check your SSH connection/permissions.${NC}"
    exit 1
fi
