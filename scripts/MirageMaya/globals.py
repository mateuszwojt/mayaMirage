from maya import cmds, mel

# NOTE: this builds the Render Settings window's custom "Mirage Renderer"
# tab (registered via cmds.renderer(..., addGlobalsTab=...) in register.py) -
# a separate UI surface from the generic Attribute Editor panel for a
# MirageRendererGlobalsNode selected directly (e.g. via the Outliner), which
# is instead covered by the standard maya.app.flux AETemplate in
# ae_template.py. The controls below (attrFieldSliderGrp/
# attrEnumOptionMenuGrp) are already attribute-bound and refresh their own
# displayed values automatically when the underlying attribute changes by
# any means, including a scripted setAttr - update_render_globals_tab()
# doesn't need to (and shouldn't try to) re-push values into them itself;
# its real job is structural updates, like enabling/disabling controls
# based on other attributes' values (see below).


def create_render_globals_node():
    if cmds.objExists("defaultMirageRenderGlobals"):
        return

    cmds.createNode(
        "MirageRendererGlobalsNode",
        name = "defaultMirageRenderGlobals",
        shared = True,
        skipSelect = True)


def delete_render_globals_node():
    if cmds.objExists("defaultMirageRenderGlobals"):
        cmds.delete("defaultMirageRenderGlobals")


def create_render_globals_tab():
    create_render_globals_node()

    parent_form = cmds.setParent(query=True)
    cmds.setUITemplate("renderGlobalsTemplate", pushTemplate=True)
    cmds.setUITemplate("attributeEditorTemplate", pushTemplate=True)

    column_width = 400

    cmds.scrollLayout("mirageRendererScrollLayout", horizontalScrollBarThickness=0)
    cmds.columnLayout("mirageRendererColumnLayout", adjustableColumn=True, width=column_width, rowSpacing=2)

    # = Mode =

    cmds.frameLayout("modeFrameLayout", label="Mode", collapsable=True, collapse=False)

    cmds.separator(height=2)

    cmds.attrEnumOptionMenuGrp(
        "mirageRenderType",
        label = "Type",
        columnWidth = (3, 160),
        columnAttach= (1, "left", 4),
        attribute = "defaultMirageRenderGlobals.renderType")

    cmds.attrEnumOptionMenuGrp(
        "mirageRenderMode",
        label = "Mode",
        columnWidth = (3, 160),
        columnAttach= (1, "left", 4),
        attribute = "defaultMirageRenderGlobals.renderMode")

    cmds.attrEnumOptionMenuGrp(
        "mirageRenderFilterType",
        label = "Filter type",
        columnWidth = (3, 160),
        columnAttach= (1, "left", 4),
        attribute = "defaultMirageRenderGlobals.filterType")

    cmds.setParent("..")

    # = Sampling =

    cmds.frameLayout("samplingFrameLayout", label="Sampling", collapsable=True, collapse=False)

    cmds.separator(height=2)

    cmds.attrFieldSliderGrp(
        "mirageRenderExposure",
        label = "Exposure",
        columnWidth = (3, 160),
        columnAttach= (1, "left", 4),
        minValue = 0,
        maxValue = 10,
        attribute = "defaultMirageRenderGlobals.exposure")

    cmds.attrFieldSliderGrp(
        "mirageRenderLimit",
        label = "Limit",
        columnWidth = (3, 160),
        columnAttach= (1, "left", 4),
        minValue = 0,
        maxValue = 10,
        attribute = "defaultMirageRenderGlobals.limit")

    cmds.attrFieldSliderGrp(
        "mirageRenderSamples",
        label = "Samples",
        columnWidth = (3, 160),
        columnAttach= (1, "left", 4),
        minValue = 1,
        maxValue = 4096,
        attribute = "defaultMirageRenderGlobals.numSamples")

    cmds.attrFieldSliderGrp(
        "mirageRenderMaxDepth",
        label = "Max Depth",
        columnWidth = (3, 160),
        columnAttach= (1, "left", 4),
        minValue = 1,
        maxValue = 64,
        attribute = "defaultMirageRenderGlobals.maxDepth")

    cmds.attrControlGrp(
        "mirageRenderEnableDof",
        label = "Enable DOF",
        attribute = "defaultMirageRenderGlobals.enableDof")

    cmds.setParent("..")

    # = Motion Blur =

    cmds.frameLayout("motionBlurFrameLayout", label="Motion Blur", collapsable=True, collapse=True)

    cmds.separator(height=2)

    cmds.attrControlGrp(
        "mirageMotionBlurEnabled",
        label = "Enabled",
        attribute = "defaultMirageRenderGlobals.motionBlurEnabled")

    cmds.attrFieldSliderGrp(
        "mirageShutterOpen",
        label = "Shutter Open",
        columnWidth = (3, 160),
        columnAttach= (1, "left", 4),
        minValue = -1,
        maxValue = 1,
        attribute = "defaultMirageRenderGlobals.shutterOpen")

    cmds.attrFieldSliderGrp(
        "mirageShutterClose",
        label = "Shutter Close",
        columnWidth = (3, 160),
        columnAttach= (1, "left", 4),
        minValue = -1,
        maxValue = 1,
        attribute = "defaultMirageRenderGlobals.shutterClose")

    cmds.setParent("..")

    # = Lighting =

    cmds.frameLayout("lightingFrameLayout", label="Lighting", collapsable=True, collapse=True)

    cmds.separator(height=2)

    cmds.attrFieldSliderGrp(
        "mirageLightIntensityScale",
        label = "Light Intensity Scale",
        annotation = "Mirage has no native photometric unit system for point/spot/directional "
                     "lights (they're approximated as small emissive shapes) - use this to "
                     "calibrate overall light brightness for a given scene.",
        columnWidth = (3, 160),
        columnAttach= (1, "left", 4),
        minValue = 0,
        maxValue = 10,
        attribute = "defaultMirageRenderGlobals.lightIntensityScale")

    cmds.setParent("..")

    # = AOVs =

    cmds.frameLayout("aovFrameLayout", label="AOVs", collapsable=True, collapse=True)

    cmds.separator(height=2)

    cmds.attrControlGrp(
        "mirageEnableDepthAOV",
        label = "Depth",
        attribute = "defaultMirageRenderGlobals.enableDepthAOV")

    cmds.attrControlGrp(
        "mirageEnableNormalAOV",
        label = "Normal",
        attribute = "defaultMirageRenderGlobals.enableNormalAOV")

    cmds.attrControlGrp(
        "mirageEnablePrimIdAOV",
        label = "Prim ID",
        attribute = "defaultMirageRenderGlobals.enablePrimIdAOV")

    cmds.setParent("..")

    # = Output =

    cmds.frameLayout("outputFrameLayout", label="Output", collapsable=True, collapse=True)

    cmds.separator(height=2)

    cmds.attrEnumOptionMenuGrp(
        "mirageOutputImageFormat",
        label = "Image Format",
        annotation = "Mirage only supports 8-bit PNG/JPG/BMP/TGA output, not Maya's full format list.",
        columnWidth = (3, 160),
        columnAttach= (1, "left", 4),
        attribute = "defaultMirageRenderGlobals.outputImageFormat")

    cmds.setParent("..")

    # = Instancing =

    cmds.frameLayout("instancingFrameLayout", label="Instancing", collapsable=True, collapse=True)

    cmds.separator(height=2)

    cmds.attrControlGrp(
        "mirageEnableInstancing",
        label = "Enable Instancing",
        annotation = "Disable to force every instance to bake its own private mesh copy, even "
                     "ones that would otherwise share GPU memory - useful for isolating "
                     "instancing-related issues.",
        attribute = "defaultMirageRenderGlobals.enableInstancing")

    cmds.setParent("..")

    # = NLM Filtering =

    cmds.frameLayout("nlmFrameLayout", label="NLM", collapsable=True, collapse=True)

    cmds.separator(height=2)

    cmds.attrFieldSliderGrp(
        "mirageRenderNLMWidth",
        label = "Width",
        columnWidth = (3, 160),
        columnAttach= (1, "left", 4),
        minValue = 1,
        attribute = "defaultMirageRenderGlobals.nlmWidth")

    cmds.attrFieldSliderGrp(
        "mirageRenderNLMFalloff",
        label = "Falloff",
        columnWidth = (3, 160),
        columnAttach= (1, "left", 4),
        minValue = 0,
        attribute = "defaultMirageRenderGlobals.nlmFalloff")

    cmds.setUITemplate("renderGlobalsTemplate", popTemplate=True)
    cmds.setUITemplate("attributeEditorTemplate", popTemplate=True)
    # NOTE: this name must match the scrollLayout created above exactly -
    # cmds object names are case-sensitive, and "MirageRendererScrollLayout"
    # (capital M) doesn't exist; only "mirageRendererScrollLayout" does. The
    # previous, uppercase version of this string caused
    # `RuntimeError: Object 'MirageRendererScrollLayout' not found` the
    # moment this tab was opened - meaning these attachForm calls, which are
    # what stretch the scroll layout to fill the Render Settings window,
    # never actually ran (the whole tab-build function raised before
    # reaching them), leaving the tab at its unstretched natural size.
    cmds.formLayout(
        parent_form,
        edit=True,
        attachForm=[
            ("mirageRendererScrollLayout", "top", 0),
            ("mirageRendererScrollLayout", "bottom", 0),
            ("mirageRendererScrollLayout", "left", 0),
            ("mirageRendererScrollLayout", "right", 0)])

    update_render_globals_tab()


def update_render_globals_tab():
    # The individual controls above are attribute-bound (attrFieldSliderGrp/
    # attrEnumOptionMenuGrp/attrControlGrp) and already refresh their own
    # displayed values automatically on any attribute change, scripted or
    # otherwise - this callback's job is structural: the shutter controls
    # are meaningless while motion blur is off, so grey them out instead of
    # just leaving them live and confusing.
    if not (cmds.control("mirageShutterOpen", exists=True) and cmds.control("mirageShutterClose", exists=True)):
        return

    motion_blur_enabled = cmds.getAttr("defaultMirageRenderGlobals.motionBlurEnabled")
    cmds.attrFieldSliderGrp("mirageShutterOpen", edit=True, enable=motion_blur_enabled)
    cmds.attrFieldSliderGrp("mirageShutterClose", edit=True, enable=motion_blur_enabled)


# Register create Mirage Renderer Tab
mel.eval("""
global proc createMirageRenderGlobalsTab() {
    python("import MirageMaya; MirageMaya.create_render_globals_tab()");
}""")

# Register update Mirage Renderer Tab
mel.eval("""
global proc updateMirageRenderGlobalsTab() {
    python("import MirageMaya; MirageMaya.update_render_globals_tab()");
}""")
