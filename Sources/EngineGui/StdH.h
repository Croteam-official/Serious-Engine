/* Copyright (c) 2002-2012 Croteam Ltd. 
This program is free software; you can redistribute it and/or modify
it under the terms of version 2 of the GNU General Public License as published by
the Free Software Foundation


This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA. */

//fixing undefined pointer struct referenced in winnt.h
#define POINTER_64 __ptr64
#include <afxcmn.h>			// MFC support for Windows Common Controls
#include <commdlg.h>

#define ENGINE_INTERNAL 1
#define ENGINEGUI_EXPORTS 1
#include <EngineGUI/EngineGUI.h>

#include "Resource.h"
#include "DlgSelectMode.h"
#include "WndDisplayTexture.h"
#include "DlgChooseTextureType.h"
#include "DlgCreateNormalTexture.h"
#include "DlgCreateAnimatedTexture.h"
#include "DlgCreateEffectTexture.h"

// this is needed for resource setting
#ifndef NDEBUG
  #define ENGINEGUI_DLL_NAME "EngineGUID.dll"
#else
  #define ENGINEGUI_DLL_NAME "EngineGUI.dll"
#endif


//#include "windows.h"
