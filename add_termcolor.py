import re
import sys
import os
import glob

# Color options
COLORS = [
    ("red", "error"),
    ("yellow", "warning"),
    ("green", "success"),
    ("cyan", "info"),
    ("blue", "notice"),
    ("magenta", "emphasis"),
    ("white", "normal"),
]

def auto_detect_color(msg):
    msg = msg.lower()
    if any(w in msg for w in ["error", "fail", "invalid", "cannot", "unable"]):
        return "red"
    if any(w in msg for w in ["warn", "caution", "attention"]):
        return "yellow"
    if any(w in msg for w in ["success", "complete", "finish", "done"]):
        return "green"
    if any(w in msg for w in ["info", "status", "progress"]):
        return "cyan"
    if any(w in msg for w in ["note", "notice"]):
        return "blue"
    if any(w in msg for w in ["important", "critical"]):
        return "magenta"
    return "white"

def show_context(lines, idx):
    ANSI = {
        'highlight': '\033[1;37m',  # bold white text
        'reset': '\033[0m',
    }
    start = max(0, idx - 3)
    end = min(len(lines), idx + 4)
    for i in range(start, end):
        prefix = ">>> " if i == idx else "    "
        if i == idx:
            print(f"{prefix}{ANSI['highlight']}{i+1:4}: {lines[i].rstrip()}{ANSI['reset']}")
        else:
            print(f"{prefix}{i+1:4}: {lines[i].rstrip()}")

def choose_color(msg, lines=None, idx=None):
    ANSI = {
        'red': '\033[31m',
        'yellow': '\033[33m',
        'green': '\033[32m',
        'cyan': '\033[36m',
        'blue': '\033[34m',
        'magenta': '\033[35m',
        'white': '\033[37m',
        'reset': '\033[0m',
        'highlight': '\033[1;37m',  # bold white text
    }
    if lines is not None and idx is not None:
        print("\nContext:")
        show_context(lines, idx)
    print(f"\nMessage: \"{msg}\"")
    for i, (color, desc) in enumerate(COLORS, 1):
        print(f"{i}) {ANSI[color]}{color:8}{ANSI['reset']} ({desc})")
    print("8) Skip this message")
    print("9) Auto-detect based on content")
    autodetect = auto_detect_color(msg)
    print(f"0) Quit")
    prompt = f"Choose option (1-9, 0 to quit, or press enter for {ANSI[autodetect]}{autodetect}{ANSI['reset']} autodetected): "
    while True:
        choice = input(prompt)
        if choice == "0":
            sys.exit(0)
        if choice == "8":
            return None
        if choice == "9" or choice == "":
            color = autodetect
            print(f"Auto-detected color: {ANSI[color]}{color}{ANSI['reset']}")
            return color
        if choice in map(str, range(1, 8)):
            return COLORS[int(choice)-1][0]
        print("Invalid choice.")

def process_line(line, color):
    # Only process if not already colored
    if "termcolor::" in line:
        return line

    # Find the first std::cout or std::cerr
    m = re.search(r'(std::c(?:out|err)\s*<<\s*)', line)
    if not m:
        return line

    # Insert color after first <<
    start = m.end()
    before = line[:start]
    after = line[start:]

    # Insert reset before the last semicolon in the line
    # (ignoring whitespace and comments after the semicolon)
    semi_idx = after.rfind(';')
    if semi_idx != -1:
        after = after[:semi_idx] + ' << termcolor::reset' + after[semi_idx:]
    else:
        # If no semicolon, just append at the end
        after = after.rstrip('\n') + ' << termcolor::reset\n'

    return before + f'termcolor::{color} << ' + after

def process_file(filename, batch_mode=False):
    with open(filename, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    changed = False
    new_lines = []
    for idx, line in enumerate(lines):
        if re.search(r'std::c(?:out|err)\s*<<', line) and 'termcolor::' not in line:
            msg_match = re.search(r'<<\s*"([^"]*)"', line)
            msg = msg_match.group(1) if msg_match else line.strip()
            color = auto_detect_color(msg) if batch_mode else choose_color(msg, lines, idx)
            if color:
                new_line = process_line(line, color)
                if new_line != line:
                    print(f"\nOriginal: {line.strip()}\nModified: {new_line.strip()}")
                    changed = True
                    line = new_line
        new_lines.append(line)

    # Ensure termcolor include is present
    include_line = '#include <termcolor/termcolor.hpp>\n'
    if any('termcolor/termcolor.hpp' in l for l in new_lines):
        pass
    else:
        # Insert after last #include
        for i in reversed(range(len(new_lines))):
            if new_lines[i].startswith('#include'):
                new_lines.insert(i+1, include_line)
                changed = True
                break

    if changed:
        with open(filename, 'w', encoding='utf-8') as f:
            f.writelines(new_lines)
        print(f"Updated {filename} with termcolor formatting.")
    else:
        print(f"No changes made to {filename}.")

def main():
    import argparse
    parser = argparse.ArgumentParser(description="Add termcolor to std::cout/cerr statements in C++ files.")
    parser.add_argument('--auto', action='store_true', help='Batch mode: auto-detect color')
    parser.add_argument('--file', type=str, help='Process only this file')
    args = parser.parse_args()

    if args.file:
        files = [args.file]
    else:
        files = [f for f in glob.glob('**/*.cpp', recursive=True) if 'third-party/' not in f and 'build/' not in f]

    for file in files:
        process_file(file, batch_mode=args.auto)

if __name__ == '__main__':
    main()