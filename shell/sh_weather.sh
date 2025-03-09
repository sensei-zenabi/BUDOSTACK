#!/bin/bash
# Muurame Weather Report Updater using ansiweather
#
# This script retrieves and displays the current weather report for Muurame
# using ansiweather, which can be installed via:
#   sudo apt install ansiweather
#
# Design Principles:
# - Uses a continuous while loop to update the weather report every hour.
# - Clears the screen for a fresh display of the report.
# - Retrieves weather data for "Muurame,FI" (Finland) in metric units.
# - Displays a header with the current date and time.
#
# Usage:
#   ./muurame_weather.sh
#
# Note: Ensure that ansiweather is installed on your system.

while true; do
    # Clear the terminal screen.
    clear
    
    # Print a header with the current date and time.
	echo "------------------- WEATHER REPORT: Muurame -------------------"
	echo ""
	curl wttr.in/Muurame?M2AdFnQ

    # Wait for one hour (3600 seconds) before the next update.
    sleep 3600
done
