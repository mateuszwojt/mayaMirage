import maya.cmds
import maya.mel

mirageMenu = None


def createMenu():
    global mirageMenu
    deleteMenu()

    mainWindow = maya.mel.eval('$temp1=$gMainWindow')
    mirageMenu = maya.cmds.menu("MirageMenu", parent = mainWindow, label = "Mirage", tearOff = True)

    maya.cmds.menuItem(divider=True, parent="MirageMenu")


def deleteMenu():
    global mirageMenu
    try:
        maya.cmds.deleteUI(mirageMenu)
    except:
        pass
