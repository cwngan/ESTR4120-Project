#!/bin/sh

cd /workspaces/CSCI4430-project
sudo apt update && sudo apt -y install clangd
sudo apt install -y pulseaudio opus-tools libopus-dev alsa-utils iputils-ping
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .