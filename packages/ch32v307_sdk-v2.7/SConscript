import rtconfig
from building import *

# get current directory
cwd = GetCurrentDir()

# The set of source files associated with this SConscript file.

sdk_path = os.path.join(cwd, "EVT", "EXAM", "SRC")
patch_path = os.path.join(cwd, "rtt-patch")

src = Glob(os.path.join(sdk_path, "Core", "*.c")) + \
    Glob(os.path.join(sdk_path, "Peripheral", "src", "*.c")) + \
    Glob(os.path.join(patch_path, "*.c")) + \
    Glob(os.path.join(patch_path, "*.S"))

path = [
    os.path.join(sdk_path, "Core"),
    os.path.join(sdk_path, "Peripheral", "inc"),
    patch_path
]

group = DefineGroup(
    'Libraries',
    src,
    depend=['PKG_USING_CH32V307_SDK'],
    CPPPATH=path
)

Return('group')
