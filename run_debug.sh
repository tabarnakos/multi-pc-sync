#!/bin/bash
# Script to run debug_sync.py with the correct virtual environment

cd /home/harvey/multi-pc-sync
source ~/.python/multi-pc-sync-env/bin/activate
python3 debug_sync.py "$@"
