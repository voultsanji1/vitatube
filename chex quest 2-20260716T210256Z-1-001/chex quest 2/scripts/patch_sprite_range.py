#!/usr/bin/env python3
"""Patch r_data.c to expand sprite range across all WADs (IWAD + PWAD)."""

with open('r_data.c', 'r') as f:
    content = f.read()

# Replace R_InitSpriteLumps to find earliest S_START and latest S_END
# across ALL loaded WADs, not just the last one (PWAD via hash).
old_init = '''    firstspritelump = W_GetNumForName (DEH_String("S_START")) + 1;
    lastspritelump = W_GetNumForName (DEH_String("S_END")) - 1;'''

new_init = '''    // VITA: find widest sprite range across ALL WADs (IWAD + PWAD)
    {
        unsigned int si;
        int earliest_start = -1, latest_end = -1;
        for (si = 0; si < numlumps; si++) {
            if (!strncasecmp(lumpinfo[si].name, "S_START", 8) ||
                !strncasecmp(lumpinfo[si].name, "SS_START", 8)) {
                if (earliest_start < 0 || (int)si < earliest_start)
                    earliest_start = (int)si;
            }
            if (!strncasecmp(lumpinfo[si].name, "S_END", 8) ||
                !strncasecmp(lumpinfo[si].name, "SS_END", 8)) {
                if ((int)si > latest_end)
                    latest_end = (int)si;
            }
        }
        if (earliest_start < 0) earliest_start = W_GetNumForName(DEH_String("S_START"));
        if (latest_end < 0) latest_end = W_GetNumForName(DEH_String("S_END"));
        firstspritelump = earliest_start + 1;
        lastspritelump = latest_end - 1;
    }'''

if old_init in content:
    content = content.replace(old_init, new_init)
    print('Patched R_InitSpriteLumps: expanded sprite range across all WADs')
else:
    print('WARNING: Could not find R_InitSpriteLumps pattern')

# Also fix R_InitFlats similarly
old_flat = '''    firstflat = W_GetNumForName (DEH_String("F_START")) + 1;
    lastflat = W_GetNumForName (DEH_String("F_END")) - 1;'''

new_flat = '''    // VITA: find widest flat range across ALL WADs
    {
        unsigned int fi;
        int earliest_fstart = -1, latest_fend = -1;
        for (fi = 0; fi < numlumps; fi++) {
            if (!strncasecmp(lumpinfo[fi].name, "F_START", 8) ||
                !strncasecmp(lumpinfo[fi].name, "FF_START", 8)) {
                if (earliest_fstart < 0 || (int)fi < earliest_fstart)
                    earliest_fstart = (int)fi;
            }
            if (!strncasecmp(lumpinfo[fi].name, "F_END", 8) ||
                !strncasecmp(lumpinfo[fi].name, "FF_END", 8)) {
                if ((int)fi > latest_fend)
                    latest_fend = (int)fi;
            }
        }
        if (earliest_fstart < 0) earliest_fstart = W_GetNumForName(DEH_String("F_START"));
        if (latest_fend < 0) latest_fend = W_GetNumForName(DEH_String("F_END"));
        firstflat = earliest_fstart + 1;
        lastflat = latest_fend - 1;
    }'''

if old_flat in content:
    content = content.replace(old_flat, new_flat)
    print('Patched R_InitFlats: expanded flat range across all WADs')

# Make sure strings.h is included for strncasecmp
if '#include <strings.h>' not in content:
    content = '#include <strings.h>\n' + content

with open('r_data.c', 'w') as f:
    f.write(content)

print('r_data.c patched successfully')
