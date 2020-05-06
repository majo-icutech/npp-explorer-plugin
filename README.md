# npp-explorer-plugin
[![Build status](https://ci.appveyor.com/api/projects/status/0wi7na0rng1k3df3?svg=true)](https://ci.appveyor.com/project/oviradoi/npp-explorer-plugin)

This is a modified version of the [Explorer] plugin for [Notepad++].

The goal was:
- to fix an annoying bug that caused the toolbar's tooltip of the plugin to display garbage text.
- make the fg/bg colors of the listview and the treeview in the plugin to always match the current Notepad++ theme.
- to fix a bug that caused icons to be displayed incorrectly
- to make the plugin 64-bit compatible

## Installation

It's easiest to just use the **Plugins > Plugins Admin** menu item built into Notepad++ v7.6.3 and newer.

For older versions (prior to Notepad++ v7.6), you could just drop the **Explorer.dll** into NPP's plugins folder -

* 32-bit Notepad++: `C:\Program Files (x86)\Notepad++\plugins`
* 64-bit Notepad++: `C:\Program Files\Notepad++\plugins`

For manual installation in Notepad++ v7.6.3 and newer, you need to put it into a subdirectory named **Explorer**, as in

* 32-bit Notepad++: `C:\Program Files (x86)\Notepad++\plugins\Explorer\`
* 64-bit Notepad++: `C:\Program Files\Notepad++\plugins\Explorer\`

### Requirements

This plugin requires the Microsoft Visual C++ Redistributable 2015 (or 2015/2017/2019).  That can be downloaded from Microsoft at https://support.microsoft.com/en-us/help/2977003/the-latest-supported-visual-c-downloads

## Screenshot
![screenshot]

<sub>This project is licensed under the terms of the GNU GPL v2.0 license<br/>
See [here][original] for the original license as published by the author of this plugin</sub>

[Explorer]: http://sourceforge.net/projects/npp-plugins/files/Explorer/
[Notepad++]: http://notepad-plus-plus.org/
[screenshot]: https://raw.githubusercontent.com/oviradoi/npp-explorer-plugin/master/images/screenshot.png "Screenshot"
[original]: https://github.com/oviradoi/npp-explorer-plugin/tree/master/Explorer
