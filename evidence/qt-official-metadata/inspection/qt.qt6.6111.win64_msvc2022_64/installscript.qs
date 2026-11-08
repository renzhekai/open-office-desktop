/****************************************************************************
**
** Copyright (C) 2026 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the release tools of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

// constructor
function Component()
{
    // add dependency to mocwrapper to install it first
    if (installer.componentByName("mocwrapper"))
        component.addDependency("mocwrapper");
}

function createShortcuts()
{
    var qtStringVersion = "6.11.1";
    var linkPostfix = qtStringVersion + " (MSVC 2022 64-bit)";
    // Create a batch file with the development environment
    var component_root_path = installer.value("TargetDir") + "/6.11.1/msvc2022_64";
    component_root_path = component_root_path.replace(/\//g, "\\");

    var batchFileName = component_root_path + "\\" + "bin" + "\\" + "qtenv2.bat";
    var contentString = "@echo off\r\n";
    contentString += "echo Setting up environment for Qt usage...\r\n";
    contentString += "set PATH="  + component_root_path + "\\bin;%PATH%\r\n";
    contentString += "cd /D " + component_root_path + "\r\n";
    contentString += "echo Remember to call vcvarsall.bat to complete environment setup!\r\n";
    // Dump batch file
    component.addOperation("AppendFile", batchFileName, contentString);

    var windir = installer.environmentVariable("WINDIR");
    if (windir == "") {
        QMessageBox["warning"]( "Error" , "Error", "Could not find windows installation directory");
        return;
    }

    var cmdLocation = windir + "\\system32\\cmd.exe";
    component.addOperation( "CreateShortcut",
                            cmdLocation,
                            "@StartMenuDir@/" + qtStringVersion + "/MSVC 2022 (64-bit)/Qt " + linkPostfix + ".lnk",
                            "/A /Q /K " + batchFileName);
    // Assistant
    component.addOperation( "CreateShortcut",
                            component_root_path + "/bin/assistant.exe",
                            "@StartMenuDir@/" + qtStringVersion + "/MSVC 2022 (64-bit)/Assistant " + linkPostfix + ".lnk");

    // Designer
    component.addOperation( "CreateShortcut",
                            component_root_path + "/bin/designer.exe",
                            "@StartMenuDir@/" + qtStringVersion + "/MSVC 2022 (64-bit)/Designer " + linkPostfix + ".lnk");

    // Linguist
    component.addOperation( "CreateShortcut",
                            component_root_path + "/bin/linguist.exe",
                            "@StartMenuDir@/" + qtStringVersion + "/MSVC 2022 (64-bit)/Linguist " + linkPostfix + ".lnk");
}

var Dir = new function ()
{
    this.toNativeSeparator = function (path)
    {
        path = path.replace(/\/\//g, '/');
        if (systemInfo.productType === "windows")
            return path.replace(/\//g, '\\');
        return path;
    }
};

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (installer.value("os") == "win") {
        try {
            var qtPath = "@TargetDir@" + "/6.11.1/msvc2022_64";
            var qmakeBinary = "@TargetDir@" + "/6.11.1/msvc2022_64/bin/qmake.exe";

            // Create a batch file and shortcuts with the development environment
            createShortcuts();

            if (installer.value("SDKToolBinary") == "")
                return;

            component.addOperation("Execute",
                                   ["@SDKToolBinary@", "addQt",
                                    "--id", component.name,
                                    "--name", "Qt %{Qt:Version} MSVC2022 64bit",
                                    "--type", "Qt4ProjectManager.QtVersion.Desktop",
                                    "--qmake", qmakeBinary,
                                    "UNDOEXECUTE", "{0,2}",
                                    "@SDKToolBinary@", "rmQt", "--id", component.name]);

            var kitName = component.name + "_kit";
            component.addOperation("Execute",
                                   ["@SDKToolBinary@", "addKit",
                                    "--id", kitName,
                                    "--name", "Desktop Qt %{Qt:Version} MSVC2022 64bit",
                                    "--Ctoolchain", "x86-windows-msvc2022-pe-64bit",
                                    "--Cxxtoolchain", "x86-windows-msvc2022-pe-64bit",
                                    "--qt", component.name,
                                    "--debuggerengine", "4",
                                    "--devicetype", "Desktop",
                                    "UNDOEXECUTE", "{0,2}",
                                    "@SDKToolBinary@", "rmKit", "--id", kitName]);

            // create qt.conf file
            createQtConfFiles(component, installer.value("TargetDir") + "/6.11.1/msvc2022_64", "Qt-6.11.1");

            // install mocwrapper
            if (installer.componentByName("mocwrapper")) {
                install_mocwrapper(component, "@TargetDir@" + "/6.11.1/msvc2022_64" + "/bin");
                // add operation for installing Qt version string file
                var qtVersionFilePath = installer.value("TargetDir") + "/6.11.1/msvc2022_64";
                qtVersionFilePath += "/bin/mocwrapper_qt_version";
                qtVersionFilePath = installer.toNativeSeparators(qtVersionFilePath);
                component.addOperation("AppendFile", qtVersionFilePath, "6.11.1");
            }
        } catch( e ) {
            print( e );
        }
    }
}

