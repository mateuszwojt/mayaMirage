from maya.app.flux.ae.Template import Template
from maya.app.flux.ae.Custom import Custom

# Attribute Editor template for MirageRendererGlobalsNode, registered via
# maya.app.flux.ae.api.registerTemplate (see register.py) - confirmed
# against the actual installed Maya 2027 runtime as the current recommended
# Python AE-template mechanism (even Autodesk's own first-party MASH plugin
# uses this, not the older pymel/classic AETemplate convention). Replaces
# the ad-hoc attrEnumOptionMenuGrp/attrFieldSliderGrp UI in globals.py,
# which only ever covered the Render Settings window's custom Mirage tab
# (a separate UI surface - see globals.py's own comment), never the node's
# generic Attribute Editor panel at all.
class MirageGlobalsControls(Custom):
    def buildUI(self, nodeName):
        with self.frameLayout("Mode", expanded=True):
            self.addControl("renderType")
            self.addControl("renderMode")
            self.addControl("filterType")

        with self.frameLayout("Sampling", expanded=True):
            self.addControl("exposure")
            self.addControl("limit")
            self.addControl("numSamples")
            self.addControl("maxDepth")
            self.addControl("enableDof")

        with self.frameLayout("Motion Blur", expanded=False):
            self.addControl("motionBlurEnabled")
            self.addControl("shutterOpen")
            self.addControl("shutterClose")

        with self.frameLayout("Lighting", expanded=False):
            self.addControl("lightIntensityScale")

        with self.frameLayout("AOVs", expanded=False):
            self.addControl("enableDepthAOV")
            self.addControl("enableNormalAOV")
            self.addControl("enablePrimIdAOV")

        with self.frameLayout("Output", expanded=False):
            self.addControl("outputImageFormat")

        with self.frameLayout("Instancing", expanded=False):
            self.addControl("enableInstancing")

        with self.frameLayout("NLM Denoising", expanded=False):
            self.addControl("nlmWidth")
            self.addControl("nlmFalloff")


class AETemplate(Template):
    def buildUI(self, nodeName):
        self.addCustom(MirageGlobalsControls(nodeName))


def register():
    import maya.app.flux.ae.api as aeApi
    aeApi.registerTemplate("MirageMaya.ae_template.AETemplate", "MirageRendererGlobalsNode")
