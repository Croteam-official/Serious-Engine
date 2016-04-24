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


#define ENGINE_INTERNAL 1
#define ENGINE_EXPORTS 1

#include <stdlib.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <conio.h>
#include <string.h>
#include <stddef.h>
#include <time.h>
#include <math.h>
#include <search.h>   // for qsort
#include <float.h>    // for FPU control

//fixing undefined pointer struct referenced in winnt.h
#if !define POINTER_64
#define POINTER_64 __ptr64
#endif 

#include <crtdbg.h>

#include <winsock2.h>
#include <windows.h>
#include <mmsystem.h> // for timers

#include <Engine/Base/Types.h>
#include <Engine/Base/Assert.h>
