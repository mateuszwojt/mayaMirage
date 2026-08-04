import maya.cmds as cmds

from .render_procedure import register_render_procedure
from . import ae_template

MIRAGE_RENDERER = "MirageRenderer"
MIRAGE_NAME = "Mirage Renderer"

def register():
    register_render_procedure()
    ae_template.register()
    cmds.renderer(MIRAGE_RENDERER, rendererUIName=MIRAGE_NAME, renderProcedure="mirageRenderProcedure")

    # create default render tabs
    cmds.renderer(MIRAGE_RENDERER, edit=True, addGlobalsNode="defaultRenderGlobals")
    cmds.renderer(MIRAGE_RENDERER, edit=True, addGlobalsTab=("Common", "createMayaSoftwareCommonGlobalsTab", "updateMayaSoftwareGlobalsTab"))
    
    # create Mirage render globals
    cmds.renderer(MIRAGE_RENDERER, edit=True, addGlobalsNode="defaultMirageRenderGlobals")
    cmds.renderer(MIRAGE_RENDERER, edit=True, addGlobalsTab=(MIRAGE_NAME, "createMirageRenderGlobalsTab", "updateMirageRenderGlobalsTab"))

def unregister():
    cmds.renderer(MIRAGE_RENDERER, unregisterRenderer=True)
