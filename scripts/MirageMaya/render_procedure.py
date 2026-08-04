from maya import mel


def register_render_procedure():
    # Maya's classic `renderer -renderProcedure` mechanism always invokes the
    # registered procedure *positionally* with these six arguments (confirmed
    # against Autodesk's own devkit sample,
    # devkit/plug-ins/renderViewInteractiveRenderCmd/registerSampleRenderer.mel:
    # `sampleRendererRender(int $width, int $height, int $doShadows,
    # int $doGlowPass, string $camera, string $option)`), never with named
    # flags. `MirageRenderProcedure` (the compiled MPxCommand) only accepts
    # named -width/-height/-camera flags and hard-fails without them, so it
    # cannot be registered as the -renderProcedure target directly - this
    # thin MEL proc is the actual target and forwards into it.
    # $doShadows/$doGlowPass are dropped: Mirage is a single-pass path
    # tracer with no separate shadow-only or glow-pass render mode.
    mel.eval("""
    global proc string mirageRenderProcedure(int $width, int $height, int $doShadows, int $doGlowPass, string $camera, string $option)
    {
        MirageRenderProcedure -width $width -height $height -camera $camera;
        return "mirageRenderProcedure";
    }
    """)
