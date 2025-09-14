#!/bin/bash

SERVICE_NAME=pacemon
EXEC_PATH=/usr/local/bin/pacemon
SERVICE_DIR=/etc/systemd/system
#CONFIG_DIR=/etc/pacemon/config
#CONFIG_FILE=$CONFIG_DIR/pacemon.config
SERVICE_FILE=$SERVICE_DIR/${SERVICE_NAME}.service
SYS_CONFIG_FILE=/etc/sysconfig/pacemaker

# Check if pacemon exists in the current directory
if [[ ! -f ./pacemon ]]; then
  echo "Warning: 'pacemon' executable not found in current directory. Installation aborted."
  exit 1
fi

# Check if /etc/sysconfig/pacemaker exists
if [[ ! -f "$SYS_CONFIG_FILE" ]]; then
  echo "Error: Pacemaker $SYS_CONFIG_FILE not found. Installation aborted."
  exit 1
fi

# Extract PCMK_logfile value if present
PCMK_LOGFILE_LINE=$(grep "^PCMK_logfile=" "$SYS_CONFIG_FILE" || true)

if [[ -n "$PCMK_LOGFILE_LINE" ]]; then
  PCMK_LOGFILE_PATH="${PCMK_LOGFILE_LINE#PCMK_logfile=}"

  # Remove possible quotes
  PCMK_LOGFILE_PATH="${PCMK_LOGFILE_PATH%\"}"
  PCMK_LOGFILE_PATH="${PCMK_LOGFILE_PATH#\"}"

  PACEMAKER_LOG_DIR=$(dirname "$PCMK_LOGFILE_PATH")
else
  PACEMAKER_LOG_DIR="/var/log/pacemaker"
fi

echo "Using Pacemaker log directory: $PACEMAKER_LOG_DIR"

# Copy pacemon and set permissions
echo "Disable service and copy pacemon to $EXEC_PATH ..."
systemctl stop pacemon
cp ./pacemon "$EXEC_PATH"
chmod +x "$EXEC_PATH"

# # Create config directory if it doesn't exist
# if [[ ! -d $CONFIG_DIR ]]; then
#   echo "Creating config directory $CONFIG_DIR ..."
#   mkdir -p "$CONFIG_DIR"
# fi

# # Write config file
# cat > "$CONFIG_FILE" <<EOF
# LOG_FILE_PATH=${PACEMAKER_LOG_DIR}/pacemaker.log
# OUTPUT_LOG_FILE_PATH=~/pacemaker_output.txt
# EOF
# echo "Configuration file created at $CONFIG_FILE"

# Create systemd service file
cat <<EOF > $SERVICE_FILE
[Unit]
Description=Pacemon Service
After=network.target

[Service]
Type=simple
ExecStart=$EXEC_PATH
Restart=on-failure
RestartSec=5
User=root
StandardOutput=syslog
StandardError=syslog
SyslogIdentifier=pacemon

[Install]
WantedBy=multi-user.target
EOF

# Reload systemd
systemctl daemon-reload
#systemctl enable $SERVICE_NAME
#systemctl start $SERVICE_NAME

# Show service status
systemctl status $SERVICE_NAME

echo "Pacemon service is installed."
echo "Service file is $SERVICE_NAME"
echo ""
echo "If you want to make this start on boot: systemctl enable $SERVICE_NAME"
echo "If you want to start it now           : systemctl start  $SERVICE_NAME"
echo ""
echo "If you make any changes to pacemon.c, compile it: gcc -o pacemon_ssh pacemon_ssh.c -lpthread"
echo "and copy the pacemon to $EXEC_PATH so you can restart the service."
echo ""
echo "Follow the log with: journalctl -u pacemon"
echo ""
