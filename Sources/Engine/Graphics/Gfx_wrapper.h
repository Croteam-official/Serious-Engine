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

#pragma once


enum GfxBlend
{
  GFX_ONE           = 21,
  GFX_ZERO          = 22,
  GFX_SRC_COLOR     = 23,
  GFX_INV_SRC_COLOR = 24,
  GFX_DST_COLOR     = 25,
  GFX_INV_DST_COLOR = 26,
  GFX_SRC_ALPHA     = 27,
  GFX_INV_SRC_ALPHA = 28,
};

enum GfxComp
{
  GFX_NEVER         = 41,
  GFX_LESS          = 42,
  GFX_LESS_EQUAL    = 43,
  GFX_EQUAL         = 44,
  GFX_NOT_EQUAL     = 45,
  GFX_GREATER_EQUAL = 46,
  GFX_GREATER       = 47,
  GFX_ALWAYS        = 48,
};
  
enum GfxFace
{
  GFX_NONE  = 61,
  GFX_FRONT = 62,
  GFX_BACK  = 63,
  GFX_CW    = 64,
  GFX_CCW   = 65,
};

enum GfxMatrixType
{
  GFX_VIEW       = 71,
  GFX_PROJECTION = 72,
};

enum GfxWrap
{
  GFX_REPEAT = 81,
  GFX_CLAMP  = 82,
};

enum GfxPolyMode
{
  GFX_FILL  = 91,
  GFX_LINE  = 92,
  GFX_POINT = 93,
};


// functions initialization for OGL, D3D or NONE (dummy)
extern void GFX_SetFunctionPointers( INDEX iAPI);


// enable operations
extern void (*gfxEnableDepthWrite)(void);
extern void (*gfxEnableDepthBias)(void);
extern void (*gfxEnableDepthTest)(void);
extern void (*gfxEnableAlphaTest)(void);
extern void (*gfxEnableBlend)(void);
extern void (*gfxEnableDither)(void);
extern void (*gfxEnableTexture)(void);
extern void (*gfxEnableClipping)(void);
extern void (*gfxEnableClipPlane)(void);

// disable operations
extern void (*gfxDisableDepthWrite)(void);
extern void (*gfxDisableDepthBias)(void);
extern void (*gfxDisableDepthTest)(void);
extern void (*gfxDisableAlphaTest)(void);
extern void (*gfxDisableBlend)(void);
extern void (*gfxDisableDither)(void);
extern void (*gfxDisableTexture)(void);
extern void (*gfxDisableClipping)(void);
extern void (*gfxDisableClipPlane)(void);

// set blending operations
extern void (*gfxBlendFunc)( GfxBlend eSrc, GfxBlend eDst);

// set depth buffer compare mode
extern void (*gfxDepthFunc)( GfxComp eFunc);
    
// set depth buffer range
extern void (*gfxDepthRange)( FLOAT fMin, FLOAT fMax);

// color mask control (use CT_RMASK, CT_GMASK, CT_BMASK, CT_AMASK to enable specific channels)
extern void (*gfxSetColorMask)( ULONG ulColorMask);
extern ULONG gfxGetColorMask(void);



// PROJECTIONS


// set face culling
extern void (*gfxCullFace)(  GfxFace eFace);
extern void (*gfxFrontFace)( GfxFace eFace);

// set custom clip plane (if NULL, disable it)
extern void (*gfxClipPlane)( const DOUBLE *pdPlane);

// set orthographic matrix
extern void (*gfxSetOrtho)( const FLOAT fLeft, const FLOAT fRight,
                            const FLOAT fTop,  const FLOAT fBottom,
                            const FLOAT fNear, const FLOAT fFar, const BOOL bSubPixelAdjust);
// set frustrum matrix
extern void (*gfxSetFrustum)( const FLOAT fLeft, const FLOAT fRight,
                              const FLOAT fTop,  const FLOAT fBottom,
                              const FLOAT fNear, const FLOAT fFar);
// set view matrix 
extern void (*gfxSetViewMatrix)( const FLOAT *pfMatrix);

// set texture matrix
extern void (*gfxSetTextureMatrix)( const FLOAT *pfMatrix);


// polygon mode (point, line or fill)
extern void (*gfxPolygonMode)( GfxPolyMode ePolyMode);



// TEXTURES


// texture settings (holds current states of texture quality, size and such)
struct TextureSettings {
public:
  //quailties
  INDEX ts_iNormQualityO;    
  INDEX ts_iNormQualityA;
  INDEX ts_iAnimQualityO;
  INDEX ts_iAnimQualityA;
  // sizes/forcing
  PIX ts_pixNormSize;
  PIX ts_pixAnimSize;
  // texture formats (set by OGL or D3D)
  ULONG ts_tfRGB8, ts_tfRGBA8;               // true color
  ULONG ts_tfRGB5, ts_tfRGBA4, ts_tfRGB5A1;  // high color
  ULONG ts_tfLA8,  ts_tfL8;                  // grayscale
  ULONG ts_tfCRGB, ts_tfCRGBA;               // compressed formats
  // maximum texel-byte ratio for largest texture size
  INDEX ts_iMaxBytesPerTexel;
};
// singleton object for texture settings
extern struct TextureSettings TS;
// routine for updating texture settings from console variable
extern void UpdateTextureSettings(void);


// texture parameters for texture state changes
class CTexParams {
public:
  INDEX tp_iFilter;            // OpenGL texture mapping mode
  INDEX tp_iAnisotropy;        // texture degree of anisotropy (>=1.0f; 1.0=isotropic, default)
  BOOL  tp_bSingleMipmap;      // texture has only one mipmap
  GfxWrap tp_eWrapU, tp_eWrapV;  // wrapping states
  inline CTexParams(void) { Clear(); tp_bSingleMipmap = FALSE; };
  inline void Clear(void) { tp_iFilter = 00; tp_iAnisotropy = 0; tp_eWrapU = tp_eWrapV = (GfxWrap)NONE; };
  inline BOOL IsEqual( CTexParams tp) { return tp_iFilter==tp.tp_iFilter && tp_iAnisotropy==tp.tp_iAnisotropy && 
                                               tp_eWrapU==tp.tp_eWrapU && tp_eWrapV==tp.tp_eWrapV; };
};

// get current texture filtering mode
extern void gfxGetTextureFiltering( INDEX &iFilterType, INDEX &iAnisotropyDegree);
// set texture filtering
extern void gfxSetTextureFiltering( INDEX &iFilterType, INDEX &iAnisotropyDegree);
// set texture LOD biasing
extern void gfxSetTextureBiasing( FLOAT &fLODBias);

// set texture wrapping mode
extern void (*gfxSetTextureWrapping)( enum GfxWrap eWrapU, enum GfxWrap eWrapV);

// set texture modulation mode (1X or 2X)
extern void (*gfxSetTextureModulation)( INDEX iScale);

// set texture unit as active
extern void gfxSetTextureUnit( INDEX iUnit);

// generate texture for API
extern void (*gfxGenerateTexture)( ULONG &ulTexObject);
// unbind texture from API
extern void (*gfxDeleteTexture)( ULONG &ulTexObject);


// set texture as current
//  - ulTexture = bind number for OGL, or *LPDIRECT3DTEXTURE8 for D3D (pointer to pointer!)
extern void gfxSetTexture( ULONG &ulTexObject, CTexParams &tpLocal);

// upload texture
// - ulTexture  = bind number for OGL, or LPDIRECT3DTEXTURE8 for D3D
// - pulTexture = pointer to texture in 32-bit R,G,B,A format (in that byte order)
// - ulFormat   = format in which the texture will be stored in accelerator's (or driver's) memory
// - bNoDiscard = no need to discard old texture (for OGL, this is like "use SubImage")
extern void gfxUploadTexture( ULONG *pulTexture, PIX pixWidth, PIX pixHeight, ULONG ulFormat, BOOL bNoDiscard);

// returns size of uploaded texture
extern SLONG gfxGetTextureSize( ULONG ulTexObject, BOOL bHasMipmaps=TRUE);

// returns bytes/pixels ratio for uploaded texture or texture format
extern INDEX gfxGetTexturePixRatio( ULONG ulTextureObject);
extern INDEX gfxGetFormatPixRatio(  ULONG ulTextureFormat);



// VERTEX ARRAYS

// prepare arrays for API
extern void (*gfxSetVertexArray)( GFXVertex4 *pvtx, INDEX ctVtx);
extern void (*gfxSetNormalArray)( GFXNormal *pnor);
extern void (*gfxSetTexCoordArray)( GFXTexCoord *ptex, BOOL b4); // b4 = projective mapping (4 FLOATS)
extern void (*gfxSetColorArray)( GFXColor *pcol);


// draw prepared arrays
extern void (*gfxDrawElements)( INDEX ctElem, INDEX *pidx);

// set constant color for subsequent rendering (until 1st gfxSetColorArray() call!)
extern void (*gfxSetConstantColor)(COLOR col);
// color array usage control
extern void (*gfxEnableColorArray)(void);
extern void (*gfxDisableColorArray)(void);


// MISC


// force finish of rendering queue
extern void (*gfxFinish)(void);


// compiled vertex array control
extern void (*gfxLockArrays)(void);
extern void gfxUnlockArrays(void);


// helper functions for drawing simple primitives thru drawelements

inline void gfxResetArrays(void)
{
  _avtxCommon.PopAll();
  _atexCommon.PopAll();
  _acolCommon.PopAll();
  _aiCommonElements.PopAll();
}
  
// render elements to screen buffer
extern void gfxFlushElements(void);
extern void gfxFlushQuads(void);


// check GFX errors only in debug builds
#ifndef NDEBUG
  extern void OGL_CheckError(void);
  extern void D3D_CheckError(HRESULT hr);
  #define OGL_CHECKERROR     OGL_CheckError();
  #define D3D_CHECKERROR(hr) D3D_CheckError(hr);
#else
  #define OGL_CHECKERROR     (void)(0);
  #define D3D_CHECKERROR(hr) (void)(0);
#endif



// ATI's TRUFORM support


// set truform parameters
extern void gfxSetTruform( const INDEX iLevel, BOOL bLinearNormals);
extern void (*gfxEnableTruform)( void);
extern void (*gfxDisableTruform)(void);


// set D3D vertex shader only if different than last time
extern void d3dSetVertexShader(DWORD dwHandle);


// macro for releasing D3D objects
#define D3DRELEASE(object,check) \
{ \
  INDEX ref; \
  do { \
    ref = (object)->Release(); \
    if(check) ASSERT(ref==0); \
  } while(ref>0);  \
  object = NONE; \
}
