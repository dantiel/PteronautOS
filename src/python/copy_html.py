Import("env")
import os, shutil, sys

os.chdir('html')
target_name = env['PIOENV'].upper()
build_flags_str = str(env['BUILD_FLAGS'])

type = 'tx' if '_TX_' in target_name else 'rx'
is8285 = '-8285' if 'ESP8285' in env['PIOENV'] else ''

# PteronautOS override: uses its own branded WebUI header
if '-DPTERONAUTOS=1' in build_flags:
    if type == 'tx':
        sys.stderr.write('ERROR: PteronautOS is RX-only, cannot build for TX target\n')
        sys.exit(1)
    if not is8285:
        sys.stderr.write('ERROR: PteronautOS requires ESP8285 target\n')
        sys.exit(1)
    chip = 'pteronautos'
else:
    chip = 'sx128x' if '-DRADIO_SX128X=1' in build_flags else 'sx127x' if '-DRADIO_SX127X=1' in build_flags else 'lr1121' if '-DRADIO_LR1121=1' in build_flags else ''

header_path = f'headers/web-{chip}-{type}{is8285}.h'
if not os.path.isfile(header_path):
    sys.stderr.write(f'ERROR: WebUI header not found: {header_path}\n')
    sys.stderr.write(f'Run "npm run build:pteronautos" (or equivalent) in src/html/ first.\n')
    sys.exit(1)

shutil.copy(header_path, '../include/WebContent.h')