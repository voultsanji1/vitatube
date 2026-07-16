#!/usr/bin/env python3
"""Patch r_things.c to allow PWAD sprites to override IWAD sprites
and to skip invalid sprite lumps instead of crashing.

Uses line-by-line scanning to handle any whitespace indentation.
"""

with open('r_things.c', 'r') as f:
    lines = f.readlines()

count = 0
i = 0
while i < len(lines):
    line = lines[i]

    # ---- Fix 1: Replace I_Error calls about duplicate sprites ----
    # Match lines containing these specific error messages
    if 'I_Error' in line and (
        'multip rot=0 lump' in line or
        'and a rot=0 lump' in line or
        'has two lumps mapped to it' in line or
        'has rotations' in line
    ):
        indent = line[:len(line) - len(line.lstrip())]
        lines[i] = indent + '{ /* VITA: allow PWAD override */ }\n'
        j = i + 1
        while j < len(lines):
            stripped = lines[j].strip()
            if stripped.startswith('"') or stripped.startswith('spritename') or stripped.startswith("'"):
                should_stop = stripped.endswith(');')
                lines[j] = ''
                j += 1
                if should_stop:
                    break
            else:
                break
        count += 1
        i = j
        continue

    # Also match multi-line I_Error about sprites
    if 'I_Error' in line and 'R_InitSprites' in line:
        combined = line
        for k in range(1, 4):
            if i + k < len(lines):
                combined += lines[i + k]
        if ('multip rot=0 lump' in combined or
            'and a rot=0 lump' in combined or
            'has two lumps mapped to it' in combined):
            indent = line[:len(line) - len(line.lstrip())]
            lines[i] = indent + '{ /* VITA: allow PWAD override */ }\n'
            j = i + 1
            while j < len(lines):
                stripped = lines[j].strip()
                if (stripped.startswith('"') or
                    stripped.startswith('spritename') or
                    stripped.startswith("'")):
                    should_stop = stripped.endswith(');')
                    lines[j] = ''
                    j += 1
                    if should_stop:
                        break
                else:
                    break
            count += 1
            i = j
            continue

    # ---- Fix 2: Replace "Bad frame characters" I_Error with continue ----
    if 'I_Error' in line and 'Bad frame characters' in line:
        indent = line[:len(line) - len(line.lstrip())]
        lines[i] = indent + 'return; /* VITA: skip bad lump instead of crash */\n'
        # Remove continuation line if present
        j = i + 1
        while j < len(lines):
            stripped = lines[j].strip()
            if stripped.startswith('"') and stripped.endswith(');'):
                lines[j] = ''
                j += 1
                break
            elif stripped.startswith('"'):
                lines[j] = ''
                j += 1
            else:
                break
        count += 1
        i = j
        continue

    # Also handle multi-line "Bad frame characters"
    if 'I_Error' in line and 'R_InstallSpriteLump' in line:
        combined = line
        for k in range(1, 3):
            if i + k < len(lines):
                combined += lines[i + k]
        if 'Bad frame characters' in combined:
            indent = line[:len(line) - len(line.lstrip())]
            lines[i] = indent + 'return; /* VITA: skip bad lump instead of crash */\n'
            j = i + 1
            while j < len(lines):
                stripped = lines[j].strip()
                if stripped.startswith('"') and stripped.endswith(');'):
                    lines[j] = ''
                    j += 1
                    break
                elif stripped.startswith('"'):
                    lines[j] = ''
                    j += 1
                else:
                    break
            count += 1
            i = j
            continue

    i += 1

# ---- Fix 3: Add frame/rotation validation in R_InitSpriteDefs ----
# Find the line where frame and rotation are calculated from lump names
# and add bounds checking before calling R_InstallSpriteLump
output = []
for i, line in enumerate(lines):
    output.append(line)
    # After: frame = lumpinfo[l].name[4] - 'A';
    # Add validation
    if "lumpinfo[l].name[4] - 'A'" in line and 'frame' in line:
        indent = line[:len(line) - len(line.lstrip())]
        # Check if next line has rotation assignment
        if i + 1 < len(lines) and "lumpinfo[l].name[5] - '0'" in lines[i + 1]:
            pass  # We'll add validation after rotation line
    if "lumpinfo[l].name[5] - '0'" in line and 'rotation' in line:
        # Check if validation already added
        if i + 1 < len(lines) and 'VITA: skip invalid' not in lines[i + 1]:
            indent = line[:len(line) - len(line.lstrip())]
            output.append('\n')
            output.append(indent + '/* VITA: skip lumps with invalid frame/rotation */\n')
            output.append(indent + 'if (frame < 0 || frame >= 29 || rotation < 0 || rotation > 8)\n')
            output.append(indent + '    continue;\n')
            output.append('\n')
            count += 1

if count > 0:
    with open('r_things.c', 'w') as f:
        f.writelines(output)
    print(f'Patched {count} items in r_things.c')
    print('r_things.c patched successfully')
else:
    print('WARNING: No patterns found - may already be patched')
