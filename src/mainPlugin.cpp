#include <maya/MDrawRegistry.h>
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MIOStream.h>

#include "RenderProcedure.h"
#include "nodes/RenderGlobals.h"


MStatus initializePlugin(MObject obj)
{
	MStatus status;
	MFnPlugin plugin(obj, "Mirage", "1.0", "any", &status);
	if (status != MS::kSuccess)
	{
		CHECK_MSTATUS(status);
		return status;
	}

	CHECK_MSTATUS(plugin.registerNode(RenderGlobalsNode::name, RenderGlobalsNode::id, RenderGlobalsNode::creator, RenderGlobalsNode::initialize));
	CHECK_MSTATUS(plugin.registerCommand(RenderProcedure::name, RenderProcedure::creator, RenderProcedure::createSyntax));

	CHECK_MSTATUS(MGlobal::executePythonCommand("import MirageMaya"));
	CHECK_MSTATUS(MGlobal::executePythonCommand("import MirageMaya.register; MirageMaya.register.register()"));
	CHECK_MSTATUS(MGlobal::executePythonCommand("import MirageMaya.menu; MirageMaya.menu.createMenu()"));

	return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj)
{
	MFnPlugin plugin(obj);
    
    CHECK_MSTATUS(plugin.deregisterCommand(RenderProcedure::name));

	CHECK_MSTATUS(MGlobal::executePythonCommand("import MirageMaya.register; MirageMaya.register.unregister()"));
	CHECK_MSTATUS(MGlobal::executePythonCommand("import MirageMaya.menu; MirageMaya.menu.deleteMenu()"));

	RenderGlobalsNode::clean();
	CHECK_MSTATUS(plugin.deregisterNode(RenderGlobalsNode::id));

	return MS::kSuccess;
}