Import("env")
import os, shutil, sys

os.chdir('html')
target_name = env['PIOENV'].upper()
build_flags_str = str(env['BUILD_FLAGS'])

type = 'tx' if '_TX_' in target_name else 'rx'
is8285 = '-8285' if 'ESP8285' in env['PIOENV'] else ''

# PteronautOS override: uses its own branded WebUI header
if '-DPTERONAUTOS=1' in build_flags_str or '-D PTERONAUTOS=1' in build_flags_str:
    if type == 'tx':
        sys.stderr.write('ERROR: PteronautOS is RX-only, cannot build for TX target\n')
        sys.exit(1)
    if not is8285:
        sys.stderr.write('ERROR: PteronautOS requires ESP8285 target\n')
        sys.exit(1)
    chip = 'pteronautos'
else:
    chip = 'sx128x' if '-DRADIO_SX128X=1' in build_flags_str else 'sx127x' if '-DRADIO_SX127X=1' in build_flags_str else 'lr1121' if '-DRADIO_LR1121=1' in build_flags_str else ''

header_path = f'headers/web-{chip}-{type}{is8285}.h'
if not os.path.isfile(header_path):
    if chip == 'pteronautos':
        fallback = f'headers/web-sx128x-{type}{is8285}.h'
        if os.path.isfile(fallback):
            sys.stderr.write(f'WARNING: PteronautOS WebUI not built, using {fallback}.\n')
            sys.stderr.write(f'Run "npm run build:pteronautos" in src/html/ for branded UI.\n')
            header_path = fallback
        else:
            sys.stderr.write(f'ERROR: No WebUI header found (tried {header_path} and {fallback})\n')
            sys.exit(1)
    else:
        sys.stderr.write(f'ERROR: WebUI header not found: {header_path}\n')
        sys.stderr.write(f'Run the appropriate npm build command in src/html/ first.\n')
        sys.exit(1)

shutil.copy(header_path, '../include/WebContent.h')