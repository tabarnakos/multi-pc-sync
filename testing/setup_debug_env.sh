#!/bin/bash
# Setup script for multi-pc-sync debug environment

echo "Setting up multi-pc-sync debug environment..."

# Create virtual environment directory
mkdir -p ~/.python

# Create virtual environment if it doesn't exist
if [ ! -d ~/.python/multi-pc-sync-env ]; then
    echo "Creating virtual environment..."
    python3 -m venv ~/.python/multi-pc-sync-env
fi

# Activate virtual environment and install requirements
echo "Installing requirements..."
source ~/.python/multi-pc-sync-env/bin/activate
pip install -r requirements.txt

echo "Setup complete!"
echo "To run the debug script, use: ./run_debug.sh"
echo "Or manually: source ~/.python/multi-pc-sync-env/bin/activate && python3 debug_sync.py"
