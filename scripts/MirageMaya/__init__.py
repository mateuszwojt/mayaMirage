# NOTE: deliberately does NOT do `from .register import register, unregister`
# here. mainPlugin.cpp always calls the submodule-qualified form
# (`import MirageMaya.register; MirageMaya.register.register()`) - a
# package-level re-export of the same names would rebind `MirageMaya.register`
# to the *function*, permanently shadowing the submodule attribute Python's
# import machinery would otherwise set on the package. That's exactly what
# used to happen here, causing `AttributeError: 'function' object has no
# attribute 'register'` the moment initializePlugin ran
# `MirageMaya.register.register()` - the shadowing function had already
# replaced the submodule reference by the time that line executed.
from .globals import (create_render_globals_node, delete_render_globals_node, create_render_globals_tab, update_render_globals_tab)