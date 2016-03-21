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

#include "stdh.h"

#include <Engine/Entities/Entity.h>
#include <Engine/Entities/EntityClass.h>
#include <Engine/Entities/EntityProperties.h>
#include <Engine/Entities/LastPositions.h>
#include <Engine/Entities/EntityCollision.h>
#include <Engine/Entities/Precaching.h>
#include <Engine/Entities/ShadingInfo.h>
#include <Engine/Light/LightSource.h>

#include <Engine/Math/Geometry.inl>
#include <Engine/Math/Clipping.inl>
#include <Engine/Math/Float.h>
#include <Engine/Math/OBBox.h>
#include <Engine/Math/Functions.h>

#include <Engine/Base/CRC.h>
#include <Engine/Base/Console.h>
#include <Engine/Base/Statistics_Internal.h>
#include <Engine/Network/Network.h>
#include <Engine/Network/PlayerTarget.h>
#include <Engine/Network/SessionState.h>
#include <Engine/Brushes/Brush.h>
#include <Engine/Brushes/BrushTransformed.h>
#include <Engine/Brushes/BrushArchive.h>
#include <Engine/Terrain/TerrainArchive.h>
#include <Engine/World/World.h>
#include <Engine/World/WorldRayCasting.h>
#include <Engine/World/PhysicsProfile.h>
#include <Engine/Base/ReplaceFile.h>
#include <Engine/Entities/InternalClasses.h>
#include <Engine/Models/ModelObject.h>
#include <Engine/Sound/SoundData.h>
#include <Engine/Sound/SoundObject.h>
#include <Engine/Graphics/Texture.h>
#include <Engine/Ska/Render.h>
#include <Engine/Terrain/Terrain.h>
#include <Engine/Terrain/TerrainRayCasting.h>
#include <Engine/Terrain/TerrainMisc.h>

#include <Engine/Base/ListIterator.inl>

#include <Engine/Templates/BSP.h>
#include <Engine/Templates/DynamicArray.cpp>
#include <Engine/Templates/DynamicContainer.cpp>
#include <Engine/Templates/StaticArray.cpp>
#include <Engine/Templates/StaticStackArray.cpp>

#include <Engine/Templates/Stock_CAnimData.h>
#include <Engine/Templates/Stock_CTextureData.h>
#include <Engine/Templates/Stock_CModelData.h>
#include <Engine/Templates/Stock_CSoundData.h>

// a reference to a void event for use as default parameter
const EVoid _evVoid;
const CEntityEvent &_eeVoid = _evVoid;

// allocation step for state stack of a CRationalEntity
#define STATESTACK_ALLOCATIONSTEP 5
extern INDEX _ctEntities;
extern INDEX _ctPredictorEntities;

// check if entity is of given class
BOOL IsOfClass(CEntity *pen, const char *pstrClassName)
{
  if (pen==NULL || pstrClassName==NULL) {
    return FALSE;
  }
  if (strcmp(pen->GetClass()->ec_pdecDLLClass->dec_strName, pstrClassName)==0) {
    return TRUE;
  } else {
    return FALSE;
  }
}
BOOL IsOfSameClass(CEntity *pen1, CEntity *pen2)
{
  if (pen1==NULL || pen2==NULL) {
    return FALSE;
  }
  if (pen1->GetClass()->ec_pdecDLLClass == pen2->GetClass()->ec_pdecDLLClass) {
    return TRUE;
  } else {
    return FALSE;
  }
}

// check if entity is of given class or derived from
BOOL IsDerivedFromClass(CEntity *pen, const char *pstrClassName)
{
  if (pen==NULL || pstrClassName==NULL) {
    return FALSE;
  }
  // for all classes in hierarchy of the entity
  for(CDLLEntityClass *pdecDLLClass = pen->GetClass()->ec_pdecDLLClass;
      pdecDLLClass!=NULL;
      pdecDLLClass = pdecDLLClass->dec_pdecBase) {
    // if it is the wanted class
    if (strcmp(pdecDLLClass->dec_strName, pstrClassName)==0) {
      // it is derived
      return TRUE;
    }
  }
  // otherwise, it is not derived
  return FALSE;
}

/////////////////////////////////////////////////////////////////////
// CEntity

/*
 * Default constructor.
 */
CEntity::CEntity(void)
{
  en_pbrBrush = NULL;
  en_psiShadingInfo = NULL;
  en_pciCollisionInfo = NULL;
  en_pecClass = NULL;
  en_ulFlags = 0;
  en_ulSpawnFlags = 0xFFFFFFFFL;    // active always
  en_ulPhysicsFlags = 0;
  en_ulCollisionFlags = 0;
  en_ctReferences = 0;
  en_ulID = 0;
  en_RenderType = RT_NONE;
  en_fSpatialClassificationRadius = -1.0f;
  en_penParent = NULL;
  en_plpLastPositions = NULL;
  _ctEntities++;
}

/*
 * Destructor.
 */
CEntity::~CEntity(void)
{
  ASSERT(en_ctReferences==0);
  ASSERT(en_ulID!=0);
  ASSERT(en_RenderType==RT_NONE);
  // remove it from container in its world
  ASSERT(!en_pwoWorld->wo_cenEntities.IsMember(this));
  en_pwoWorld->wo_cenAllEntities.Remove(this);

  // unset spatial clasification
  en_rdSectors.Clear();

  /*
  Models are always destructed on End(), but brushes and terrains are not, so
  if the pointer is not NULL, then it must be a brush or terrain.
  Both of them are derived from CBrushBase so GetBrushType() will return its real type 
  */

  // if it is brush of terrain
  if(en_pbrBrush != NULL) {
    INDEX btType = en_pbrBrush->GetBrushType();
    // if this is brush3d
    if(btType==CBrushBase::BT_BRUSH3D) {
      // free the brush
      en_pwoWorld->wo_baBrushes.ba_abrBrushes.Delete(en_pbrBrush);
      en_pbrBrush = NULL;
    // if this is terrain
    } else if(btType==CBrushBase::BT_TERRAIN) {
      // free the brush
      en_pwoWorld->wo_taTerrains.ta_atrTerrains.Delete(en_ptrTerrain);
      en_pbrBrush = NULL;
    // unknown type
    } else {
      ASSERTALWAYS("Unsupported brush type");
    }
  }
  // clear entity type
  en_RenderType = RT_NONE;
  en_pecClass->RemReference();
  en_pecClass = NULL;

  en_fSpatialClassificationRadius = -1.0f;
  _ctEntities--;

  if (IsPredictable()) {
    if (en_pwoWorld->wo_cenPredictable.IsMember(this)) {
      en_pwoWorld->wo_cenPredictable.Remove(this);
    }
  }
  if (en_ulFlags&ENF_WILLBEPREDICTED) {
    if (en_pwoWorld->wo_cenWillBePredicted.IsMember(this)) {
      en_pwoWorld->wo_cenWillBePredicted.Remove(this);
    }
  }
  if (IsPredictor()) {
    if (en_pwoWorld->wo_cenPredictor.IsMember(this)) {
      en_pwoWorld->wo_cenPredictor.Remove(this);
      _ctPredictorEntities--;
    }
  }
}

/////////////////////////////////////////////////////////////////////
// Access functions

/* Test if the entity is an empty brush. */
BOOL CEntity::IsEmptyBrush(void) const
{
  // if it is not brush
  if (en_RenderType != CEntity::RT_BRUSH && en_RenderType != RT_FIELDBRUSH) {
    // it is not empty brush
    return FALSE;
  // if it is brush
  } else {

    // get its brush
    CBrush3D &brBrush = *en_pbrBrush;
    // get the first mip of the brush
    CBrushMip *pbmMip = brBrush.GetFirstMip();

    // it is empty if it has zero sectors
    return pbmMip->bm_abscSectors.Count()==0;
  }
}

/* Return max Game Players */
INDEX CEntity::GetMaxPlayers(void) {
  return NET_MAXGAMEPLAYERS;
};

/* Return Player Entity */
CEntity *CEntity::GetPlayerEntity(INDEX iPlayer)
{
  ASSERT(iPlayer>=0 && iPlayer<GetMaxPlayers());

  CSessionState &ses = _pNetwork->ga_sesSessionState;
  if (ses.ses_apltPlayers[iPlayer].plt_bActive) {
    return ses.ses_apltPlayers[iPlayer].plt_penPlayerEntity;
  } else {
    return NULL;
  }
}

/* Get bounding box of this entity - for AI purposes only. */
void CEntity::GetBoundingBox(FLOATaabbox3D &box)
{
  if (en_pciCollisionInfo!=NULL) {
    box = en_pciCollisionInfo->ci_boxCurrent;
  } else {
    GetSize(box);
    box += GetPlacement().pl_PositionVector;
  }
}

/* Get size of this entity - for UI purposes only. */
void CEntity::GetSize(FLOATaabbox3D &box)
{
  if (en_RenderType==CEntity::RT_MODEL || en_RenderType==CEntity::RT_EDITORMODEL) {
    en_pmoModelObject->GetCurrentFrameBBox( box);
    box.StretchByVector(en_pmoModelObject->mo_Stretch);
  } else if(en_RenderType==CEntity::RT_SKAMODEL || en_RenderType==CEntity::RT_SKAEDITORMODEL) {
    GetModelInstance()->GetCurrentColisionBox( box);
    box.StretchByVector(GetModelInstance()->mi_vStretch);
  } else if (en_RenderType==CEntity::RT_TERRAIN) {
    GetTerrain()->GetAllTerrainBBox(box);
  } else if (en_RenderType==CEntity::RT_BRUSH || en_RenderType==CEntity::RT_FIELDBRUSH) {
    CBrushMip *pbm = en_pbrBrush->GetFirstMip();
    if (pbm == NULL) {
      box = FLOATaabbox3D(FLOAT3D(0,0,0), FLOAT3D(0,0,0));
    } else {
      box = pbm->bm_boxBoundingBox;
      box += -GetPlacement().pl_PositionVector;
    }
  }
  else {
    box = FLOATaabbox3D(FLOAT3D(0,0,0), FLOAT3D(0,0,0));
  }
}

/* Get name of this entity. */
const CTString &CEntity::GetName(void) const
{
  static const CTString strDummyName("");
  return strDummyName;
}
const CTString &CEntity::GetDescription(void) const // name + some more verbose data
{
  static const CTString strDummyDescription("");
  return strDummyDescription;
}

/* Get first target of this entity. */
CEntity *CEntity::GetTarget(void) const
{
  return NULL;
}

/* Check if entity can be used as a target. */
BOOL CEntity::IsTargetable(void) const
{
  // cannot be targeted unless this function is overridden
  return FALSE;
}
/* Check if entity is marker */
BOOL CEntity::IsMarker(void) const{
  // cannot be marker unless this function is overridden
  return FALSE;
}
/* Check if entity is important */
BOOL CEntity::IsImportant(void) const{
  // cannot be important unless this function is overridden
  return FALSE;
}
/* Check if entity is moved on a route set up by its targets. */
BOOL CEntity::MovesByTargetedRoute(CTString &strTargetProperty) const
{
  return FALSE;
}
/* Check if entity can drop marker for making linked route. */
BOOL CEntity::DropsMarker(CTFileName &fnmMarkerClass, CTString &strTargetProperty) const
{
  return FALSE;
}

/* Get light source information - return NULL if not a light source. */
CLightSource *CEntity::GetLightSource(void)
{
  return NULL;
}

BOOL CEntity::IsTargetValid(SLONG slPropertyOffset, CEntity *penTarget)
{
  return TRUE;
}

/* Get anim data for given animation property - return NULL for none. */
CAnimData *CEntity::GetAnimData(SLONG slPropertyOffset)
{
  return NULL;
}

/* Get force type name, return empty string if not used. */
const CTString &CEntity::GetForceName(INDEX iForce)
{
  static const CTString strDummyName("");
  return strDummyName;
}

/* Get forces in given point. */
void CEntity::GetForce(INDEX iForce, const FLOAT3D &vPoint,
  CForceStrength &fsGravity, CForceStrength &fsField)
{
  // default gravity
  fsGravity.fs_vDirection = FLOAT3D(0,-1,0);
  fsGravity.fs_fAcceleration = 9.81f;
  fsGravity.fs_fVelocity = 0;

  // no force field
  fsField.fs_fAcceleration = 0;
}
/* Get entity that controls the force, used for change notification checking. */
CEntity *CEntity::GetForceController(INDEX iForce)
{
  return NULL;
}

/* Adjust model shading parameters if needed - return TRUE if needs model shadows. */
BOOL CEntity::AdjustShadingParameters(FLOAT3D &vLightDirection,
  COLOR &colLight, COLOR &colAmbient)
{
  return TRUE;
}
/* Adjust model mip factor if needed. */
void CEntity::AdjustMipFactor(FLOAT &fMipFactor)
{
  (void)fMipFactor;
  NOTHING;
}

// get a different model object for rendering - so entity can change its appearance dynamically
// NOTE: base model is always used for other things (physics, etc).
CModelObject *CEntity::GetModelForRendering(void)
{
  return en_pmoModelObject;
}

// get a different model instance for rendering - so entity can change its appearance dynamically
// NOTE: base model is always used for other things (physics, etc).
CModelInstance *CEntity::GetModelInstanceForRendering(void)
{
  return en_pmiModelInstance;
}

/* Get fog type name, return empty string if not used. */
const CTString &CEntity::GetFogName(INDEX iFog)
{
  static const CTString strDummyName("");
  return strDummyName;
}

/* Get fog, return FALSE for none. */
BOOL CEntity::GetFog(INDEX iFog, class CFogParameters &fpFog)
{
  return FALSE;
}

/* Get haze type name, return empty string if not used. */
const CTString &CEntity::GetHazeName(INDEX iHaze)
{
  static const CTString strDummyName("");
  return strDummyName;
}

/* Get haze, return FALSE for none. */
BOOL CEntity::GetHaze(INDEX iHaze, class CHazeParameters &hpHaze, FLOAT3D &vViewDir)
{
  return FALSE;
}

/* Get mirror type name, return empty string if not used. */
const CTString &CEntity::GetMirrorName(INDEX iMirror)
{
  static const CTString strDummyName("");
  return strDummyName;
}

/* Get mirror, return FALSE for none. */
BOOL CEntity::GetMirror(INDEX iMirror, class CMirrorParameters &mpMirror)
{
  return FALSE;
}

/* Get gradient type name, return empty string if not used. */
const CTString &CEntity::GetGradientName(INDEX iGradient)
{
  static const CTString strDummyName("");
  return strDummyName;
}

/* Get gradient, return FALSE for none. */
BOOL CEntity::GetGradient(INDEX iGradient, class CGradientParameters &gpGradient)
{
  return FALSE;
}

FLOAT3D CEntity::GetClassificationBoxStretch(void)
{
  return FLOAT3D( 1.0f, 1.0f, 1.0f);
}

/* Get field information - return NULL if not a field. */
CFieldSettings *CEntity::GetFieldSettings(void)
{
  return NULL;
}
/* Render particles made by this entity. */
void CEntity::RenderParticles(void)
{
  NOTHING;
}
/* Get current collision box index for this entity. */
INDEX CEntity::GetCollisionBoxIndex(void)
{
  // by default, use only box 0
  return 0;
}
/* Get current collision box - override for custom collision boxes. */
void CEntity::GetCollisionBoxParameters(INDEX iBox, FLOATaabbox3D &box, INDEX &iEquality)
{
  // if this is ska model
  if(en_RenderType==RT_SKAMODEL || en_RenderType==RT_SKAEDITORMODEL) {
    box.minvect = GetModelInstance()->GetCollisionBoxMin(iBox);
    box.maxvect = GetModelInstance()->GetCollisionBoxMax(iBox);
    FLOATaabbox3D boxNS = box;
    box.StretchByVector(GetModelInstance()->mi_vStretch);
    iEquality = GetModelInstance()->GetCollisionBoxDimensionEquality(iBox);
  } else {
    box.minvect = en_pmoModelObject->GetCollisionBoxMin(iBox);
    box.maxvect = en_pmoModelObject->GetCollisionBoxMax(iBox);
    box.StretchByVector(en_pmoModelObject->mo_Stretch);
    iEquality = en_pmoModelObject->GetCollisionBoxDimensionEquality(iBox);
  }
}

/* Render game view */
void CEntity::RenderGameView(CDrawPort *pdp, void *pvUserData)
{
  NOTHING;
}
// apply mirror and stretch to the entity if supported
void CEntity::MirrorAndStretch(FLOAT fStretch, BOOL bMirrorX)
{
  NOTHING;
}
// get offset for depth-sorting of alpha models (in meters, positive is nearer)
FLOAT CEntity::GetDepthSortOffset(void)
{
  return 0.0f;
}
// get visibility tweaking bits
ULONG CEntity::GetVisTweaks(void)
{
  return 0;
}

// Get max tessellation level
FLOAT CEntity::GetMaxTessellationLevel(void)
{
  return 0.0f;
}

// get pointer to your predictor/predicted
CEntity *CEntity::GetPredictionPair(void)
{
  // this should never be called, it must be overriden if prediction is used!
  ASSERT(FALSE);
  return this;  // this is safest to return if in release version
}
void CEntity::SetPredictionPair(CEntity *penPair)
{
  // this should never be called, it must be overriden if prediction is used!
  ASSERT(FALSE);
}

// add to prediction any entities that this entity depends on
void CEntity::AddDependentsToPrediction(void)
{
}

// called by other entities to set time prediction parameter
void CEntity::SetPredictionTime(TIME tmAdvance)   // give time interval in advance to set
{
  NOTHING; // by default, don't use time prediction
}

// called by engine to get the upper time limit 
TIME CEntity::GetPredictionTime(void)   // return moment in time up to which to predict this entity
{
  return -1.0f; // by default, don't use time prediction
}

// get maximum allowed range for predicting this entity
FLOAT CEntity::GetPredictionRange(void)
{
  return UpperLimit(0.0f);    // by default, cli_fPredictEntitiesRange is the limit
}

// copy for prediction
void CEntity::CopyForPrediction(CEntity &enOrg)
{
  // this should never be called, it must be overriden if prediction is used!
  ASSERT(FALSE);
}

CEntity *CEntity::GetPredictor(void)
{
  ASSERT(IsPredicted());
  CEntity *pen = GetPredictionPair();
  ASSERT(pen->IsPredictor());
  return pen;
}
CEntity *CEntity::GetPredicted(void)
{
  ASSERT(IsPredictor());
  CEntity *pen = GetPredictionPair();
  ASSERT(pen!=NULL);
  ASSERT(pen->IsPredicted());
  return pen;
}
// become predictable/unpredictable
void CEntity::SetPredictable(BOOL bON)
{
  // if predictor
  if (IsPredictor()) {
    // do nothing
    return;
  }

  // if already set
  if (IsPredictable()) {
    // check that valid
    ASSERT(en_pwoWorld->wo_cenPredictable.IsMember(this));
    // if turning on
    if (bON) {
      // do nothing
      return;
    }
    // mark as not predictable
    en_ulFlags&=~ENF_PREDICTABLE;
    // remove from container
    en_pwoWorld->wo_cenPredictable.Remove(this);

  // if not set
  } else {
    // check that valid
    ASSERT(!en_pwoWorld->wo_cenPredictable.IsMember(this));
    ASSERT(!IsPredictor());
    ASSERT(!IsPredicted());
    // if turning off
    if (!bON) {
      // do nothing
      return;
    }
    // mark as predictable
    en_ulFlags|=ENF_PREDICTABLE;
    // add to container
    en_pwoWorld->wo_cenPredictable.Add(this);
  }
}

// check if this instance is head of prediction chain
BOOL CEntity::IsPredictionHead(void)
{
  // if predicted, or will be predicted, or is a temporary predictor
  if (en_ulFlags&(ENF_PREDICTED|ENF_WILLBEPREDICTED|ENF_TEMPPREDICTOR)) {
    // it cannot be head of the chain
    return FALSE;
  }
  
  // if predictor, but not currently in the last step of prediction
  if ((en_ulFlags&ENF_PREDICTOR) && 
    _pTimer->CurrentTick()<=_pNetwork->ga_sesSessionState.ses_tmPredictionHeadTick) {
    // it cannot be head of the chain
    return FALSE;
  }
  // otherwise it is head of the chain
  return TRUE;
}

// get the prediction original (predicted), or self if not predicting
CEntity *CEntity::GetPredictionTail(void)
{
  // if this is a predictor
  if (IsPredictor()) {
    // it must be head of the prediction chain
    //ASSERT(IsPredictionHead());
    // get its predicted
    return GetPredicted();
  }
  // in other cases, return self
  return this;
}
// check if active for prediction now
BOOL CEntity::IsAllowedForPrediction(void) const
{
  return !_pNetwork->IsPredicting() || IsPredictor();
}
// check an event for prediction, returns true if already predicted
BOOL CEntity::CheckEventPrediction(ULONG ulTypeID, ULONG ulEventID)
{
  return _pNetwork->ga_sesSessionState.CheckEventPrediction(this, ulTypeID, ulEventID);
}

/* Called after creating and setting its properties. */
void CEntity::OnInitialize(const CEntityEvent &eeInput)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // try to find a handler in start state
  pEventHandler pehHandler = HandlerForStateAndEvent(1, eeInput.ee_slEvent);
  // if there is a handler
  if (pehHandler!=NULL) {
    // call the function
    (this->*pehHandler)(eeInput);
  // if there is no handler
  } else {
    ASSERTALWAYS("All entities must have Main procedure!");
  }
}
/* Called before releasing entity. */
void CEntity::OnEnd(void)
{
}

// print stack to debug output
const char *CEntity::PrintStackDebug(void) 
{
  return "not CRationalEntity";
};

/*
 * Prepare entity (call after setting properties).
 */
void CEntity::Initialize(const CEntityEvent &eeInput)
{
  CSetFPUPrecision FPUPrecision(FPT_24BIT);

  // make sure we are not deleted during intialization
  CEntityPointer penThis = this;

  Initialize_internal(eeInput);
  // set spatial clasification
  FindSectorsAroundEntity();
  // precache all other things
  Precache();
}
void CEntity::Initialize_internal(const CEntityEvent &eeInput)
{
  #ifndef NDEBUG
    // clear settings for debugging
    en_RenderType = RT_ILLEGAL;
  #endif

  // remember brush zoning flag
  BOOL bWasZoning = en_ulFlags&ENF_ZONING;

  // let derived class initialize according to the properties
  OnInitialize(eeInput);
  // derived class must set all properties
//  ASSERT(en_RenderType != RT_ILLEGAL);

  // if this is a brush
  if (en_RenderType==RT_BRUSH || en_RenderType==RT_FIELDBRUSH) {
    // test if zoning
    BOOL bZoning = en_ulFlags&ENF_ZONING;
    // if switching from zoning to non-zoning
    if (bWasZoning && !bZoning) {
      // switch from zoning to non-zoning
      en_pbrBrush->SwitchToNonZoning();
      en_rdSectors.Clear();
    // if switching from non-zoning to zoning
    } else if (!bWasZoning && bZoning) {
      // switch from non-zoning to zoning
      en_pbrBrush->SwitchToZoning();
      en_rdSectors.Clear();
    }
  }

  // if it is a field brush
  CFieldSettings *pfs = GetFieldSettings();
  if (pfs!=NULL) {
    // remember its field settings
    ASSERT(en_RenderType == RT_FIELDBRUSH);
    en_pbrBrush->br_pfsFieldSettings = pfs;
  }
}

/*
 * Clean-up entity.
 */
void CEntity::End(void)
{
  CSetFPUPrecision FPUPrecision(FPT_24BIT);
  /* NOTE: Must not remove from thinker/mover list here, or CServer::ProcessGameTick()
   * might crash!
   */
  End_internal();
}
void CEntity::End_internal(void)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // let derived class clean-up after itself
  OnEnd();

  // clear last positions
  if (en_plpLastPositions!=NULL) {
    delete en_plpLastPositions;
    en_plpLastPositions = NULL;
  }

  // clear spatial classification
  en_fSpatialClassificationRadius = -1.0f;
  en_boxSpatialClassification = FLOATaabbox3D();

  // depending on entity type
  switch(en_RenderType) {
  // if it is brush
  case RT_BRUSH:
    DiscardCollisionInfo();
    break;
  // if it is field brush
  case RT_FIELDBRUSH:
    DiscardCollisionInfo();
    break;

  // if it is model
  case RT_MODEL:
  case RT_EDITORMODEL:
    // free its model object
    delete en_pmoModelObject;
    delete en_psiShadingInfo;
    DiscardCollisionInfo();
    en_pmoModelObject = NULL;
    en_psiShadingInfo = NULL;
    break;
  // if it is ska model
  case RT_SKAMODEL:
  case RT_SKAEDITORMODEL:
    en_pmiModelInstance->Clear();
    delete en_pmiModelInstance;
    delete en_psiShadingInfo;
    DiscardCollisionInfo();
    en_pmiModelInstance = NULL;
    en_psiShadingInfo = NULL;
    break;
  case RT_TERRAIN:
    DiscardCollisionInfo();
    break;

  // if it is nothing
  case RT_NONE:
  case RT_VOID:
    // do nothing
    NOTHING;
    break;
  // if it is any other type
  default:
    ASSERTALWAYS("Unsupported entity type");
  }
  // clear entity type
  en_RenderType = RT_NONE;
}

/*
 * Reinitialize the entity.
 */
void CEntity::Reinitialize(void)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);
  End_internal();
  Initialize_internal(_eeVoid);
}

// teleport this entity to a new location -- takes care of telefrag damage
void CEntity::Teleport(const CPlacement3D &plNew, BOOL bTelefrag /*=TRUE*/)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);
  ASSERT(en_fSpatialClassificationRadius>0);

  // if telefragging is on and the entity has collision box
  if (bTelefrag && en_pciCollisionInfo!=NULL) {

    // create the box of the entity at its new placement
    FLOATmatrix3D mRot;
    MakeRotationMatrixFast(mRot, plNew.pl_OrientationAngle);
    FLOAT3D vPos = plNew.pl_PositionVector;
    CMovingSphere &ms0 = en_pciCollisionInfo->ci_absSpheres[0];
    CMovingSphere &ms1 = en_pciCollisionInfo->ci_absSpheres[en_pciCollisionInfo->ci_absSpheres.Count()-1];
    FLOATaabbox3D box;
    box  = FLOATaabbox3D(vPos+ms0.ms_vCenter*mRot, ms0.ms_fR);
    box |= FLOATaabbox3D(vPos+ms1.ms_vCenter*mRot, ms1.ms_fR);

    // first inflict huge damage there in the entities box
    InflictBoxDamage(this, DMT_TELEPORT, 100000.0f, box);
  }

  // remember original orientation matrix
  FLOATmatrix3D mOld = en_mRotation;

  // now put the entity there
  SetPlacement(plNew);
  // movable entity
  if (en_ulPhysicsFlags & EPF_MOVABLE) {
    ((CMovableEntity*)this)->ClearTemporaryData();
    ((CMovableEntity*)this)->en_plLastPlacement = en_plPlacement;
    // transform speeds
    FLOATmatrix3D mRel = en_mRotation*!mOld;
    ((CMovableEntity*)this)->en_vCurrentTranslationAbsolute *= mRel;
    
    if (_pNetwork->ga_ulDemoMinorVersion>=7) {
      // clear reference
      ((CMovableEntity*)this)->en_penReference = NULL;
      ((CMovableEntity*)this)->en_pbpoStandOn = NULL;
    }

    // notify that it was teleported
    SendEvent(ETeleport());
    ((CMovableEntity*)this)->AddToMovers();

    extern INDEX ent_bReportSpawnInWall;
    if (ent_bReportSpawnInWall) {
      // if movable model
      if (en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL || 
        en_RenderType==RT_SKAMODEL || en_RenderType==RT_SKAEDITORMODEL) {
        // check if it was teleported inside a wall
        CMovableModelEntity *pmme = (CMovableModelEntity*)this;
        CEntity *ppenObstacleDummy;
        if (pmme->CheckForCollisionNow(pmme->en_iCollisionBox, &ppenObstacleDummy)) {
          CPrintF("Entity '%s' was teleported inside a wall at (%g,%g,%g)!\n", 
            GetName(), 
            en_plPlacement.pl_PositionVector(1),
            en_plPlacement.pl_PositionVector(2),
            en_plPlacement.pl_PositionVector(3));
        }
      }
    }
  }
}

/*
 * Set placement of this entity. (use only in WEd!)
 */
void CEntity::SetPlacement(const CPlacement3D &plNew)
{
  CSetFPUPrecision FPUPrecision(FPT_24BIT);
  // check if orientation is changed
  BOOL bSameOrientation = (plNew.pl_OrientationAngle==en_plPlacement.pl_OrientationAngle);

  // if the orientation has not changed
  if (bSameOrientation) {
    // set the placement and do all needed recalculation
    SetPlacement_internal(plNew, en_mRotation, FALSE /* doesn't have to be near. */);

  // if the orientation has changed
  } else {
    // calculate new rotation matrix
    FLOATmatrix3D mRotation;
    MakeRotationMatrixFast(mRotation, plNew.pl_OrientationAngle);
    // set the placement and do all needed recalculation
    SetPlacement_internal(plNew, mRotation, FALSE /* doesn't have to be near. */);
  }

  // if this entity has parent
  if (en_penParent!=NULL) {
    // adjust relative placement
    en_plRelativeToParent = en_plPlacement;
    en_plRelativeToParent.AbsoluteToRelativeSmooth(en_penParent->en_plPlacement);
  }
}


/*
 * Fall down to floor. (use only in WEd!)
 */
void CEntity::FallDownToFloor( void)
{
  CEntity::RenderType rt = GetRenderType();
  // is this old model
  if(rt==CEntity::RT_MODEL || rt==CEntity::RT_EDITORMODEL) {
    ASSERT(en_pmoModelObject != NULL);
  // is this ska model
  } else if(rt==CEntity::RT_SKAMODEL || rt==CEntity::RT_SKAEDITORMODEL) {
    ASSERT(GetModelInstance() != NULL);
  } else {
    return;
  }
  // if( rt!=CEntity::RT_MODEL && rt!=CEntity::RT_EDITORMODEL) return;
  // ASSERT(en_pmoModelObject != NULL);

  CPlacement3D plPlacement = GetPlacement();
  FLOAT3D vRay[4];
  // if it is movable entity
  if( en_ulPhysicsFlags & EPF_MOVABLE) {
    INDEX iEq;
    FLOATaabbox3D box;
    GetCollisionBoxParameters(GetCollisionBoxIndex(), box, iEq);
    FLOAT3D vMin = box.Min();
    FLOAT3D vMax = box.Max();
    // all ray casts start from same height
    vRay[0](2) = vMax(2);
    vRay[1](2) = vMax(2);
    vRay[2](2) = vMax(2);
    vRay[3](2) = vMax(2);

    vRay[0](1) = vMin(1);
    vRay[0](3) = vMin(3);
    vRay[1](1) = vMin(1);
    vRay[1](3) = vMax(3);
    vRay[2](1) = vMax(1);
    vRay[2](3) = vMin(3);
    vRay[3](1) = vMax(1);
    vRay[3](3) = vMax(3);
  }
  else {
    FLOATaabbox3D box;
    if(rt==CEntity::RT_SKAMODEL || rt==CEntity::RT_SKAEDITORMODEL) {
      GetModelInstance()->GetCurrentColisionBox( box);
    } else {
      en_pmoModelObject->GetCurrentFrameBBox( box);
    }

    FLOAT3D vCenterUp = box.Center();
    vCenterUp(2) = box.Max()(2);
    vRay[0] = vCenterUp;
    vRay[1] = vCenterUp;
    vRay[2] = vCenterUp;
    vRay[3] = vCenterUp;
  }

  FLOAT fMaxY = -9999999.0f;
  BOOL bFloorHitted = FALSE;
  for( INDEX iRay=0; iRay<4; iRay++)
  {
    FLOAT3D vSource = plPlacement.pl_PositionVector+vRay[iRay];
    FLOAT3D vTarget = vSource;
    vTarget(2) -= 1000.0f;
    CCastRay crRay( this, vSource, vTarget);
    crRay.cr_ttHitModels = CCastRay::TT_NONE; // CCastRay::TT_FULLSEETHROUGH;
    crRay.cr_bHitTranslucentPortals = TRUE;
    crRay.cr_bPhysical = TRUE;
    GetWorld()->CastRay(crRay);
    if( (crRay.cr_penHit != NULL) && (crRay.cr_vHit(2) > fMaxY)) {
      fMaxY = crRay.cr_vHit(2);
      bFloorHitted = TRUE;
    }
  }
  if( bFloorHitted) plPlacement.pl_PositionVector(2) += fMaxY-plPlacement.pl_PositionVector(2)+0.01f;
  SetPlacement( plPlacement);
}


extern CEntity *_penLightUpdating;
extern BOOL _bDontDiscardLinks = FALSE;

// internal repositioning function
void CEntity::SetPlacement_internal(const CPlacement3D &plNew, const FLOATmatrix3D &mRotation,
   BOOL bNear)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);
  _pfPhysicsProfile.StartTimer(CPhysicsProfile::PTI_SETPLACEMENT);
  _pfPhysicsProfile.IncrementTimerAveragingCounter(CPhysicsProfile::PTI_SETPLACEMENT);

  // invalidate eventual cached info for still models
  en_ulFlags &= ~ENF_VALIDSHADINGINFO;

  _pfPhysicsProfile.StartTimer(CPhysicsProfile::PTI_SETPLACEMENT_COORDSUPDATE);
  // remembel old placement of the entity
  CPlacement3D plOld = en_plPlacement;
  // set new placement of the entity
  en_plPlacement = plNew;
  en_mRotation = mRotation;
  _pfPhysicsProfile.StopTimer(CPhysicsProfile::PTI_SETPLACEMENT_COORDSUPDATE);

  // if this is a brush entity
  if (en_RenderType==RT_BRUSH || en_RenderType==RT_FIELDBRUSH) {
    _pfPhysicsProfile.StartTimer(CPhysicsProfile::PTI_SETPLACEMENT_BRUSHUPDATE);
    // recalculate all bounding boxes relative to new position
    _bDontDiscardLinks = TRUE;
    en_pbrBrush->CalculateBoundingBoxes();
    _bDontDiscardLinks = FALSE;

    BOOL bHasShadows=FALSE;
    // for all brush mips
    FOREACHINLIST(CBrushMip, bm_lnInBrush, en_pbrBrush->br_lhBrushMips, itbm) {
      // for all sectors in the mip
      {FOREACHINDYNAMICARRAY(itbm->bm_abscSectors, CBrushSector, itbsc) {
        // for all polygons in this sector
        {FOREACHINSTATICARRAY(itbsc->bsc_abpoPolygons, CBrushPolygon, itbpo) {
          // if the polygon has shadows
          if (!(itbpo->bpo_ulFlags & BPOF_FULLBRIGHT)) {
            // discard shadows
            itbpo->DiscardShadows();
            bHasShadows = TRUE;
          }
        }}
      }}
    }
    // find possible shadow layers near affected area
    if (bHasShadows) {
      if (en_ulFlags&ENF_DYNAMICSHADOWS) {
        _penLightUpdating = NULL;
      } else {
        _penLightUpdating = this;
      }
      en_pwoWorld->FindShadowLayers(en_pbrBrush->GetFirstMip()->bm_boxBoundingBox,
        FALSE, FALSE /* no directional */);
      _penLightUpdating = NULL;
    }

    // if it is zoning
    if (en_ulFlags&ENF_ZONING) {
      // FPU must be in 53-bit mode
      CSetFPUPrecision FPUPrecision(FPT_53BIT);

      // for all brush mips
      FOREACHINLIST(CBrushMip, bm_lnInBrush, en_pbrBrush->br_lhBrushMips, itbm) {
        // for all sectors in the mip
        {FOREACHINDYNAMICARRAY(itbm->bm_abscSectors, CBrushSector, itbsc) {
          // find entities in sector
          itbsc->FindEntitiesInSector();
        }}
      }
    }

    _pfPhysicsProfile.StopTimer(CPhysicsProfile::PTI_SETPLACEMENT_BRUSHUPDATE);
  } else if(en_RenderType==RT_TERRAIN) {
    // Update terrain shadow map
    CTerrain *ptrTerrain = GetTerrain();
    ASSERT(ptrTerrain!=NULL);
    ptrTerrain->UpdateShadowMap();
  }

  // set spatial clasification
  _pfPhysicsProfile.StartTimer(CPhysicsProfile::PTI_SETPLACEMENT_SPATIALUPDATE);
  if (bNear) {
    FindSectorsAroundEntityNear();
  } else {
    FindSectorsAroundEntity();
  }
  _pfPhysicsProfile.StopTimer(CPhysicsProfile::PTI_SETPLACEMENT_SPATIALUPDATE);

  _pfPhysicsProfile.StartTimer(CPhysicsProfile::PTI_SETPLACEMENT_LIGHTUPDATE);
  // if it is a light source
  {CLightSource *pls = GetLightSource();
  if (pls!=NULL) {
    // find all shadow maps that should have layers from this light source
    pls->FindShadowLayers(FALSE);
    // update shadow map on all terrains in world
    pls->UpdateTerrains(plOld,en_plPlacement);
  }}

  _pfPhysicsProfile.StopTimer(CPhysicsProfile::PTI_SETPLACEMENT_LIGHTUPDATE);

  // move the entity to new position in collision grid
  if (en_pciCollisionInfo!=NULL) {
    _pfPhysicsProfile.StartTimer(CPhysicsProfile::PTI_SETPLACEMENT_COLLISIONUPDATE);
    FLOATaabbox3D boxNew;
    en_pciCollisionInfo->MakeBoxAtPlacement(
      en_plPlacement.pl_PositionVector, en_mRotation, boxNew);
    if (en_RenderType!=RT_BRUSH && en_RenderType!=RT_FIELDBRUSH) {
      en_pwoWorld->MoveEntityInCollisionGrid( this, en_pciCollisionInfo->ci_boxCurrent, boxNew);
    }
    en_pciCollisionInfo->ci_boxCurrent = boxNew;
    _pfPhysicsProfile.StopTimer(CPhysicsProfile::PTI_SETPLACEMENT_COLLISIONUPDATE);
  }

  _pfPhysicsProfile.StopTimer(CPhysicsProfile::PTI_SETPLACEMENT);

  // NOTE: this is outside profile because it uses recursion

  // for each child of this entity
  {FOREACHINLIST(CEntity, en_lnInParent, en_lhChildren, itenChild) {
    CPlacement3D plNew = itenChild->en_plRelativeToParent;
    plNew.RelativeToAbsoluteSmooth(en_plPlacement);
    itenChild->SetPlacement(plNew);
  }}
}
// this one is used in rendering - gets lerped placement between ticks
CPlacement3D CEntity::GetLerpedPlacement(void) const
{
  // if it has no parent
  if (en_penParent==NULL) {
    // no lerping
    return en_plPlacement;
  // if it has parent
  } else {
    // get lerped placement relative to parent
    CPlacement3D plParentLerped = en_penParent->GetLerpedPlacement();
    CPlacement3D plLerped = en_plRelativeToParent;
    plLerped.RelativeToAbsoluteSmooth(plParentLerped);
    return plLerped;
  }
}

void CEntity::SetFlags(ULONG ulFlags)
{
  en_ulFlags = ulFlags;
}

void CEntity::SetPhysicsFlags(ULONG ulFlags)
{
  // remember the new flags
  en_ulPhysicsFlags = ulFlags;

  // cache eventual collision info
  FindCollisionInfo();
}

void CEntity::SetCollisionFlags(ULONG ulFlags)
{
  // remember the new flags
  en_ulCollisionFlags = ulFlags;

  // cache eventual collision info
  FindCollisionInfo();
}

void CEntity::SetParent(CEntity *penNewParent)
{
  // if there is a parent already
  if (en_penParent!=NULL) {
    // remove from it
    en_penParent = NULL;
    en_lnInParent.Remove();
  }

  // if should set new parent
  if (penNewParent!=NULL) {
    // for each predecesor (parent) entity in the chain
    for (CEntity *penPred=penNewParent; penPred!=NULL; penPred=penPred->en_penParent) {
      // if self
      if (penPred==this) {
        // refuse to set parent
        return;
      }
    }

    // set new parent
    en_penParent = penNewParent;
    penNewParent->en_lhChildren.AddTail(en_lnInParent);
    // calculate relative placement
    en_plRelativeToParent = en_plPlacement;
    en_plRelativeToParent.AbsoluteToRelativeSmooth(en_penParent->en_plPlacement);
  }
}


// find first child of given class
CEntity *CEntity::GetChildOfClass(const char *strClass)
{
  // for each child of this entity
  {FOREACHINLIST(CEntity, en_lnInParent, en_lhChildren, itenChild) {
    // if it is of given class
    if (IsOfClass(itenChild, strClass)) {
      return itenChild;
    }
  }}
  // not found
  return NULL;
}  

/*
 * Destroy this entity (entity must not be targetable).
 */
void CEntity::Destroy(void)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // if it is already destroyed
  if (en_ulFlags&ENF_DELETED) {
    // do nothing
    return;
  }
  // if it is a light source
  {CLightSource *pls = GetLightSource();
  if (pls!=NULL) {
    // destroy all of its shadow layers
    pls->DiscardShadowLayers();
  }}
  // clean up the entity
  End();
  SetDefaultProperties(); // this effectively clears all entity pointers!

  // unlink parent-child links
  if (en_penParent != NULL) {
    en_penParent = NULL;
    en_lnInParent.Remove();
  }
  {FORDELETELIST( CEntity, en_lnInParent, en_lhChildren, itenChild) {
    itenChild->en_penParent = NULL;
    itenChild->en_lnInParent.Remove();
  }}

  // set its flags to mark that it doesn't not exist anymore
  en_ulFlags|=ENF_DELETED;
  // make sure that no deleted entity can be alive
  en_ulFlags&=~ENF_ALIVE;
  // remove from all sectors
  en_rdSectors.Clear();
  // remove from active entities in the world
  en_pwoWorld->wo_cenEntities.Remove(this);
  // remove the reference made by the entity itself (this can delete it!)
  RemReference();
}


FLOAT3D _vHandle;
CBrushPolygon *_pbpoNear;
CTerrain *_ptrTerrainNear;
FLOAT _fNearDistance;
FLOAT3D _vNearPoint;

static void CheckPolygonForShadingInfo(CBrushPolygon &bpo)
{
  // if it is not a wall
  if (bpo.bpo_ulFlags&(BPOF_INVISIBLE|BPOF_PORTAL) ) {
    // skip it
    return;
  }
  // if the polygon or it's entity are invisible
  if (bpo.bpo_pbscSector->bsc_pbmBrushMip->bm_pbrBrush->br_penEntity->en_ulFlags&ENF_HIDDEN) {
    // skip it
    return;
  }

  const FLOATplane3D &plPolygon = bpo.bpo_pbplPlane->bpl_plAbsolute;
  // find distance of the polygon plane from the handle
  FLOAT fDistance = plPolygon.PointDistance(_vHandle);
  // if it is behind the plane or further than nearest found
  if (fDistance<0.0f || fDistance>_fNearDistance) {
    // skip it
    return;
  }
  // find projection of handle to the polygon plane
  FLOAT3D vOnPlane = plPolygon.ProjectPoint(_vHandle);
  // if it is not in the bounding box of polygon
  const FLOATaabbox3D &boxPolygon = bpo.bpo_boxBoundingBox;
  const FLOAT EPSILON = 0.01f;
  if (
    (boxPolygon.Min()(1)-EPSILON>vOnPlane(1)) ||
    (boxPolygon.Max()(1)+EPSILON<vOnPlane(1)) ||
    (boxPolygon.Min()(2)-EPSILON>vOnPlane(2)) ||
    (boxPolygon.Max()(2)+EPSILON<vOnPlane(2)) ||
    (boxPolygon.Min()(3)-EPSILON>vOnPlane(3)) ||
    (boxPolygon.Max()(3)+EPSILON<vOnPlane(3))) {
    // skip it
    return;
  }

  // find major axes of the polygon plane
  INDEX iMajorAxis1, iMajorAxis2;
  GetMajorAxesForPlane(plPolygon, iMajorAxis1, iMajorAxis2);

  // create an intersector
  CIntersector isIntersector(_vHandle(iMajorAxis1), _vHandle(iMajorAxis2));
  // for all edges in the polygon
  FOREACHINSTATICARRAY(bpo.bpo_abpePolygonEdges, CBrushPolygonEdge, itbpePolygonEdge) {
    // get edge vertices (edge direction is irrelevant here!)
    const FLOAT3D &vVertex0 = itbpePolygonEdge->bpe_pbedEdge->bed_pbvxVertex0->bvx_vAbsolute;
    const FLOAT3D &vVertex1 = itbpePolygonEdge->bpe_pbedEdge->bed_pbvxVertex1->bvx_vAbsolute;
    // pass the edge to the intersector
    isIntersector.AddEdge(
      vVertex0(iMajorAxis1), vVertex0(iMajorAxis2),
      vVertex1(iMajorAxis1), vVertex1(iMajorAxis2));
  }

  // if the point is not inside polygon
  if (!isIntersector.IsIntersecting()) {
    // skip it
    return;
  }

  // remember the polygon
  _pbpoNear = &bpo;
  _fNearDistance = fDistance;
  _vNearPoint = vOnPlane;
}

static void CheckTerrainForShadingInfo(CTerrain *ptrTerrain)
{
  ASSERT(ptrTerrain!=NULL);
  ASSERT(ptrTerrain->tr_penEntity!=NULL);
  CEntity *pen = ptrTerrain->tr_penEntity;

  FLOAT3D vTerrainNormal;
  FLOAT3D vHitPoint;
  FLOATplane3D plHitPlane;
  vTerrainNormal = FLOAT3D(0,-1,0) * pen->en_mRotation;
  FLOAT fDistance = TestRayCastHit(ptrTerrain,pen->en_mRotation,pen->en_plPlacement.pl_PositionVector,
                                   _vHandle,_vHandle+vTerrainNormal,_fNearDistance,FALSE,plHitPlane,vHitPoint);
  if(fDistance<_fNearDistance) {
    _vNearPoint = vHitPoint;
    _fNearDistance = fDistance;
    _ptrTerrainNear = ptrTerrain;
  }
}

/* Find and remember shading info for this entity if invalid. */
void CEntity::FindShadingInfo(void)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // if this entity can't even have shading info
  if (en_psiShadingInfo==NULL) {
    // do nothing
    return;
  }

  // if info is valid
  if (en_ulFlags & ENF_VALIDSHADINGINFO) {
    // !!! check if the polygon is still there !
    // do nothing
    return;
  }
  en_ulFlags |= ENF_VALIDSHADINGINFO;

  en_psiShadingInfo->si_penEntity = this;

  // clear shading info
  en_psiShadingInfo->si_pbpoPolygon = NULL;
  en_psiShadingInfo->si_ptrTerrain  = NULL;
  if (en_psiShadingInfo->si_lnInPolygon.IsLinked()) {
    en_psiShadingInfo->si_lnInPolygon.Remove();
  }

  // take reference point at handle of the model entity
  _vHandle = en_plPlacement.pl_PositionVector;
  // start infinitely far away
  _pbpoNear = NULL;
  _ptrTerrainNear = NULL;
  _fNearDistance = UpperLimit(1.0f);
  // if this is movable entity
  if (en_ulPhysicsFlags&EPF_MOVABLE) {
    // for each cached near polygon
    CStaticStackArray<CBrushPolygon *> &apbpo = ((CMovableEntity*)this)->en_apbpoNearPolygons;
    for(INDEX iPolygon=0; iPolygon<apbpo.Count(); iPolygon++) {
      CheckPolygonForShadingInfo(*apbpo[iPolygon]);
    }
  }

  // for each sector that this entity is in
  {FOREACHSRCOFDST(en_rdSectors, CBrushSector, bsc_rsEntities, pbsc)
    // for each brush or terrain in this sector
    {FOREACHDSTOFSRC(pbsc->bsc_rsEntities, CEntity, en_rdSectors, pen)
      if(pen->en_RenderType==CEntity::RT_TERRAIN) {
        CheckTerrainForShadingInfo(pen->GetTerrain());
      } else if(pen->en_RenderType!=CEntity::RT_BRUSH && pen->en_RenderType!=CEntity::RT_FIELDBRUSH) {
        break;
      }
    }}
  ENDFOR}

  // if this is non-movable entity, or no polygon or terrain found so far
  if (_pbpoNear==NULL && _ptrTerrainNear==NULL) {
    // for each sector that this entity is in
    {FOREACHSRCOFDST(en_rdSectors, CBrushSector, bsc_rsEntities, pbsc)
      // for each polygon in the sector
      {FOREACHINSTATICARRAY(pbsc->bsc_abpoPolygons, CBrushPolygon, itbpo) {
        CBrushPolygon &bpo = *itbpo;
        CheckPolygonForShadingInfo(bpo);
      }}
    ENDFOR}
  }

  // if there is some polygon found
  if( _pbpoNear!=NULL) {
    // remember shading info
    en_psiShadingInfo->si_pbpoPolygon = _pbpoNear;
    _pbpoNear->bpo_lhShadingInfos.AddTail(en_psiShadingInfo->si_lnInPolygon);
    en_psiShadingInfo->si_vNearPoint = _vNearPoint;

    CEntity *penWithPolygon = _pbpoNear->bpo_pbscSector->bsc_pbmBrushMip->bm_pbrBrush->br_penEntity;
    ASSERT(penWithPolygon!=NULL);
    const FLOATmatrix3D &mPolygonRotation = penWithPolygon->en_mRotation;
    const FLOAT3D &vPolygonTranslation = penWithPolygon->GetPlacement().pl_PositionVector;

    _vNearPoint = (_vNearPoint-vPolygonTranslation)*!mPolygonRotation;

    MEX2D vmexShadow;
    _pbpoNear->bpo_mdShadow.GetTextureCoordinates(
      _pbpoNear->bpo_pbplPlane->bpl_pwplWorking->wpl_mvRelative,
      _vNearPoint, vmexShadow);
    CBrushShadowMap &bsm = _pbpoNear->bpo_smShadowMap;
    INDEX iMipLevel = bsm.sm_iFirstMipLevel;
    FLOAT fpixU = FLOAT(vmexShadow(1)+bsm.sm_mexOffsetX)*(1.0f/(1<<iMipLevel));
    FLOAT fpixV = FLOAT(vmexShadow(2)+bsm.sm_mexOffsetY)*(1.0f/(1<<iMipLevel));
    en_psiShadingInfo->si_pixShadowU = floor(fpixU);
    en_psiShadingInfo->si_pixShadowV = floor(fpixV);
    en_psiShadingInfo->si_fUDRatio = fpixU-en_psiShadingInfo->si_pixShadowU;
    en_psiShadingInfo->si_fLRRatio = fpixV-en_psiShadingInfo->si_pixShadowV;

  // else if there is some terrain found
  } else if(_ptrTerrainNear!=NULL) {
    // remember shading info
    en_psiShadingInfo->si_ptrTerrain = _ptrTerrainNear;
    en_psiShadingInfo->si_vNearPoint = _vNearPoint;
    
    FLOAT2D vTc = CalculateShadingTexCoords(_ptrTerrainNear,_vNearPoint);
    en_psiShadingInfo->si_pixShadowU = floor(vTc(1));
    en_psiShadingInfo->si_pixShadowV = floor(vTc(2));
    en_psiShadingInfo->si_fLRRatio   = vTc(1) - en_psiShadingInfo->si_pixShadowU;
    en_psiShadingInfo->si_fUDRatio   = vTc(2) - en_psiShadingInfo->si_pixShadowV;

    _ptrTerrainNear->tr_lhShadingInfos.AddTail(en_psiShadingInfo->si_lnInPolygon);
  }
}

CBrushSector *CEntity::GetFirstSector(void)
{
  {FOREACHSRCOFDST(en_rdSectors, CBrushSector, bsc_rsEntities, pbsc)
    return pbsc;
  ENDFOR};
  return NULL;
}

CBrushSector *CEntity::GetFirstSectorWithName(void)
{
  CBrushSector *pbscResult = NULL;
  {FOREACHSRCOFDST(en_rdSectors, CBrushSector, bsc_rsEntities, pbsc)
    if (pbsc->bsc_strName!="") {
      pbscResult = pbsc;
      break;
    }
  ENDFOR};
  return pbscResult;
}

// max. distance between two spheres (as factor of radius of one sphere)
#define MIN_SPHEREDENSITY 1.0f

CCollisionInfo::CCollisionInfo(const CCollisionInfo &ciOrg)
{
  ci_absSpheres = ciOrg.ci_absSpheres ;
  ci_fMinHeight = ciOrg.ci_fMinHeight ;
  ci_fMaxHeight = ciOrg.ci_fMaxHeight ;
  ci_fHandleY   = ciOrg.ci_fHandleY   ;
  ci_fHandleR   = ciOrg.ci_fHandleR   ;
  ci_boxCurrent = ciOrg.ci_boxCurrent ;
  ci_ulFlags    = ciOrg.ci_ulFlags    ;
}
/* Create collision info for a model. */
void CCollisionInfo::FromModel(CEntity *penModel, INDEX iBox)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // get collision box information from the model
  FLOATaabbox3D boxModel;
  INDEX iBoxType;
  penModel->GetCollisionBoxParameters(iBox, boxModel, iBoxType);
  FLOAT3D vBoxOffset = boxModel.Center();
  FLOAT3D vBoxSize = boxModel.Size();

//  ASSERT(iBoxType==LENGHT_EQ_WIDTH);
  ci_ulFlags = 0;

  INDEX iAxisMain; // in which direction are spheres set
  INDEX iAxis1, iAxis2; // other axis

  if (iBoxType==LENGTH_EQ_WIDTH) {
    iAxisMain = 2;
    iAxis1 = 1; iAxis2 = 3;
  } else if (iBoxType==HEIGHT_EQ_WIDTH) {
    iAxisMain = 3;
    iAxis1 = 2; iAxis2 = 1;
  } else if (iBoxType==LENGTH_EQ_HEIGHT) {
    iAxisMain = 1;
    iAxis1 = 2; iAxis2 = 3;
  } else {
    ASSERTALWAYS("Invalid collision box");
    iAxisMain = 2;
    iAxis1 = 1; iAxis2 = 3;
  }

  // calculate radius of one sphere
  FLOAT fSphereRadius = vBoxSize(iAxis1)/2.0f;
  // calculate length along which to set spheres
  FLOAT fSphereCentersSpan = vBoxSize(iAxisMain)-fSphereRadius*2;
  // calculate number of spheres to use
  INDEX ctSpheres = 0;
  if (fSphereRadius>0.0001f) {
    ctSpheres = INDEX(ceil(fSphereCentersSpan/(fSphereRadius*MIN_SPHEREDENSITY)))+1;
  }
  if (ctSpheres==0) {
    ctSpheres=1;
  }
  // calculate how far from each other to set sphere centers
  FLOAT fSphereCentersDistance;
  if (ctSpheres==1) {
    fSphereCentersDistance = 0.0f;
  } else {
    fSphereCentersDistance = fSphereCentersSpan/(FLOAT)(ctSpheres-1);
  }

  // calculate coordinates for spreading sphere centers
  FLOAT fSphereCenterX = vBoxOffset(iAxis1);
  FLOAT fSphereCenterZ = vBoxOffset(iAxis2);
  FLOAT fSphereCenterY0 = vBoxOffset(iAxisMain)-(vBoxSize(iAxisMain)/2.0f)+fSphereRadius;
  FLOAT fSphereCenterKY = fSphereCentersDistance;

  ci_fMinHeight = boxModel.Min()(2);
  ci_fMaxHeight = boxModel.Max()(2);
  ci_fHandleY = UpperLimit(1.0f);
  // create needed number of spheres in the array
  ci_absSpheres.Clear();
  ci_absSpheres.New(ctSpheres);
  // for each sphere
  for(INDEX iSphere=0; iSphere<ctSpheres; iSphere++) {
    CMovingSphere &ms = ci_absSpheres[iSphere];
    // set its center and radius
    ms.ms_vCenter(iAxis1) = fSphereCenterX;
    ms.ms_vCenter(iAxis2) = fSphereCenterZ;
    ms.ms_vCenter(iAxisMain) = fSphereCenterY0+iSphere*fSphereCenterKY;
    ms.ms_fR = fSphereRadius;
    ci_fHandleY = Min(ci_fHandleY, ms.ms_vCenter(2));
  }

  // remember handle parameters
  if (ctSpheres==1 || iBoxType==LENGTH_EQ_WIDTH) {
    ci_ulFlags|=CIF_CANSTANDONHANDLE;
    ci_fHandleR = fSphereRadius;
  } else {
    ci_fHandleR = 0.0f;
  }

  // set optimization flags

  if (ctSpheres==1 &&
    ci_absSpheres[0].ms_vCenter(1)==0 &&
    ci_absSpheres[0].ms_vCenter(2)==0 &&
    ci_absSpheres[0].ms_vCenter(3)==0) {
    ci_ulFlags|=CIF_IGNOREROTATION;
  }

  if (iBoxType==LENGTH_EQ_WIDTH &&
    ci_absSpheres[0].ms_vCenter(1)==0 &&
    ci_absSpheres[0].ms_vCenter(3)==0) {
    ci_ulFlags|=CIF_IGNOREHEADING;
  }
}

/* Create collision info for a ska model */
void CCollisionInfo::FromSkaModel(CEntity *penModel, INDEX iBox)
{
}
/* Create collision info for a brush. */
void CCollisionInfo::FromBrush(CBrush3D *pbrBrush)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  ci_absSpheres.Clear();
  ci_absSpheres.New(1);
  ci_ulFlags = CIF_BRUSH;
  // clear brush's relative box
  FLOATaabbox3D box;
  // get first brush mip
  CBrushMip *pbm = pbrBrush->GetFirstMip();
  // for each sector in the brush mip
  {FOREACHINDYNAMICARRAY(pbm->bm_abscSectors, CBrushSector, itbsc) {
    // for each vertex in the sector
    {FOREACHINSTATICARRAY(itbsc->bsc_abvxVertices, CBrushVertex, itbvx) {
      CBrushVertex &bvx = *itbvx;
      // add it to bounding box
      box |= DOUBLEtoFLOAT(bvx.bvx_vdPreciseRelative);
    }}
  }}

  // create a sphere from the relative box
  ci_absSpheres[0].ms_vCenter = box.Center();
  ci_absSpheres[0].ms_fR = box.Size().Length()/2;
  ci_fMinHeight = UpperLimit(1.0f);
  ci_fMaxHeight = LowerLimit(1.0f);
  ci_fHandleY   = 0.0f;
  ci_fHandleR   = 1.0f;
}

/* Calculate current bounding box in absolute space from position. */
void CCollisionInfo::MakeBoxAtPlacement(const FLOAT3D &vPosition, const FLOATmatrix3D &mRotation,
  FLOATaabbox3D &box)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  CMovingSphere &ms0 = ci_absSpheres[0];
  CMovingSphere &ms1 = ci_absSpheres[ci_absSpheres.Count()-1];
  box  = FLOATaabbox3D(vPosition+ms0.ms_vCenter*mRotation, ms0.ms_fR);
  box |= FLOATaabbox3D(vPosition+ms1.ms_vCenter*mRotation, ms1.ms_fR);
}

// get maximum radius of entity in xz plane (relative to entity handle)
FLOAT CCollisionInfo::GetMaxFloorRadius(void)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // get first and last sphere
  CMovingSphere &ms0 = ci_absSpheres[0];
  CMovingSphere &ms1 = ci_absSpheres[ci_absSpheres.Count()-1];
  // get their positions in xz plane
  FLOAT3D vPosXZ0 = ms0.ms_vCenter;
  FLOAT3D vPosXZ1 = ms1.ms_vCenter;
  vPosXZ0(2) = 0.0f;
  vPosXZ1(2) = 0.0f;
  // return maximum distance from the handle in xz plane
  return Max(
    vPosXZ0.Length()+ms0.ms_fR, 
    vPosXZ1.Length()+ms1.ms_fR);
}


/* Find and remember collision info for this entity. */
void CEntity::FindCollisionInfo(void)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // discard eventual collision info
  DiscardCollisionInfo();

  // if the entity is colliding
  if (en_ulCollisionFlags&ECF_TESTMASK) {
    // if it is a model
    if ((en_RenderType==RT_MODEL||en_RenderType==RT_EDITORMODEL)
    &&(en_pmoModelObject->GetData()!=NULL)) {
      // cache its new collision info
      en_pciCollisionInfo = new CCollisionInfo;
      en_pciCollisionInfo->FromModel(this, GetCollisionBoxIndex());
    } else if ((en_RenderType==RT_SKAMODEL||en_RenderType==RT_SKAEDITORMODEL)
      &&(GetModelInstance()!=NULL)) {
      // cache its new collision info
      en_pciCollisionInfo = new CCollisionInfo;
      en_pciCollisionInfo->FromModel(this, GetCollisionBoxIndex());
    // if it is a brush
    } else if (en_RenderType==RT_BRUSH) {
      // if it is zoning brush and non movable
      if ((en_ulFlags&ENF_ZONING) && !(en_ulPhysicsFlags&EPF_MOVABLE)) {
        // do nothing
        return;
      }
      // cache its new collision info
      en_pciCollisionInfo = new CCollisionInfo;
      en_pciCollisionInfo->FromBrush(en_pbrBrush);
    // if it is a field brush
    } else if (en_RenderType==RT_FIELDBRUSH) {
      // cache its new collision info
      en_pciCollisionInfo = new CCollisionInfo;
      en_pciCollisionInfo->FromBrush(en_pbrBrush);
      return;
    } else if (en_RenderType==RT_TERRAIN) {
      return;
    } else {
      return;
    }
    // add entity to collision grid
    FLOATaabbox3D boxNew;
    en_pciCollisionInfo->MakeBoxAtPlacement(
      en_plPlacement.pl_PositionVector, en_mRotation, boxNew);
    if (en_RenderType!=RT_BRUSH && en_RenderType!=RT_FIELDBRUSH) {
      en_pwoWorld->AddEntityToCollisionGrid(this, boxNew);
    }
    en_pciCollisionInfo->ci_boxCurrent = boxNew;
  }
}
// discard collision info for this entity
void CEntity::DiscardCollisionInfo(void)
{
  // if there was any collision info
  if (en_pciCollisionInfo!=NULL) {
    // remove entity from collision grid
    if (en_RenderType!=RT_BRUSH && en_RenderType!=RT_FIELDBRUSH) {
      en_pwoWorld->RemoveEntityFromCollisionGrid(this, en_pciCollisionInfo->ci_boxCurrent);
    }
    // free it
    delete en_pciCollisionInfo;
    en_pciCollisionInfo = NULL;
  }
  // movable entity
  if (en_ulPhysicsFlags & EPF_MOVABLE) {
    ((CMovableEntity*)this)->ClearTemporaryData();
  }
}
// copy collision info from some other entity
void CEntity::CopyCollisionInfo(CEntity &enOrg)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // if there is no collision info
  if (enOrg.en_pciCollisionInfo==NULL) {
    // do nothing
    en_pciCollisionInfo = NULL;
    return;
  }
  // create info and copy it
  en_pciCollisionInfo = new CCollisionInfo(*enOrg.en_pciCollisionInfo);
  // add entity to collision grid
  FLOATaabbox3D boxNew;
  en_pciCollisionInfo->MakeBoxAtPlacement(
    en_plPlacement.pl_PositionVector, en_mRotation, boxNew);
  if (en_RenderType!=RT_BRUSH && en_RenderType!=RT_FIELDBRUSH) {
    en_pwoWorld->AddEntityToCollisionGrid(this, boxNew);
  }
  en_pciCollisionInfo->ci_boxCurrent = boxNew;
}

/* Get box and sphere for spatial clasification. */
void CEntity::UpdateSpatialRange(void)
{
  CSetFPUPrecision FPUPrecision(FPT_24BIT);

  en_fSpatialClassificationRadius = -1.0f;

  // if zoning
  if (en_ulFlags&ENF_ZONING) {
    // do nothing
    return;
  }

  FLOATaabbox3D box;
  FLOATaabbox3D boxStretched;
  // get bounding box of the entity
  // is this old model
  if (en_RenderType==CEntity::RT_MODEL
    ||en_RenderType==CEntity::RT_EDITORMODEL) {
    en_pmoModelObject->GetAllFramesBBox(box);
    box.StretchByVector(en_pmoModelObject->mo_Stretch);
    FLOAT3D fClassificationStretch = GetClassificationBoxStretch();
    boxStretched = box;
    boxStretched .StretchByVector( fClassificationStretch);
    en_boxSpatialClassification = boxStretched;
  // is this ska model
  } else if (en_RenderType==CEntity::RT_SKAMODEL
    || en_RenderType==RT_SKAEDITORMODEL) {
    GetModelInstance()->GetAllFramesBBox(box);
    box.StretchByVector(GetModelInstance()->mi_vStretch);
    FLOAT3D fClassificationStretch = GetClassificationBoxStretch();
    boxStretched = box;
    boxStretched.StretchByVector( fClassificationStretch);
    en_boxSpatialClassification = boxStretched;
  // is this brush
  } else if (en_RenderType==CEntity::RT_BRUSH || en_RenderType==RT_FIELDBRUSH) {
    box = en_pbrBrush->GetFirstMip()->bm_boxRelative;
    boxStretched = box;
    en_boxSpatialClassification = box;
  // is this terrain
  } else if (en_RenderType==CEntity::RT_TERRAIN) {
    GetTerrain()->GetAllTerrainBBox(box);
    boxStretched = box;
    en_boxSpatialClassification = box;
  } else {
    return; // sound entities are not related to sectors !!!!
  }
  en_fSpatialClassificationRadius = Max( box.Min().Length(), box.Max().Length() );
  ASSERT(IsValidFloat(en_fSpatialClassificationRadius));
}

/* Find and remember all sectors that this entity is in. */
void CEntity::FindSectorsAroundEntity(void)
{
  CSetFPUPrecision sfp(FPT_53BIT);

  // if not in spatial clasification
  if (en_fSpatialClassificationRadius<0) {
    // do nothing
    return;
  }
  // get bounding sphere and box of entity
  FLOAT fSphereRadius = en_fSpatialClassificationRadius;
  const FLOAT3D &vSphereCenter = en_plPlacement.pl_PositionVector;
  // make oriented bounding box of the entity
  FLOATobbox3D boxEntity = FLOATobbox3D(en_boxSpatialClassification, 
    en_plPlacement.pl_PositionVector, en_mRotation);
  DOUBLEobbox3D boxdEntity = FLOATtoDOUBLE(boxEntity);

  // unset spatial clasification
  en_rdSectors.Clear();

  // for each brush in the world
  FOREACHINDYNAMICARRAY(en_pwoWorld->wo_baBrushes.ba_abrBrushes, CBrush3D, itbr) {
    CBrush3D &br=*itbr;
    // if the brush entity is not zoning
    if (itbr->br_penEntity==NULL || !(itbr->br_penEntity->en_ulFlags&ENF_ZONING)) {
      // skip it
      continue;
    }
    // for each mip in the brush
    FOREACHINLIST(CBrushMip, bm_lnInBrush, itbr->br_lhBrushMips, itbm) {
      // for each sector in the brush mip
      FOREACHINDYNAMICARRAY(itbm->bm_abscSectors, CBrushSector, itbsc) {
        // if the sector's bounding box has contact with the sphere 
        if(itbsc->bsc_boxBoundingBox.TouchesSphere(vSphereCenter, fSphereRadius)
          // and with the box
          && boxEntity.HasContactWith(FLOATobbox3D(itbsc->bsc_boxBoundingBox))) {
          
          // if the sphere is inside the sector
          if (itbsc->bsc_bspBSPTree.TestSphere(
              FLOATtoDOUBLE(vSphereCenter), FLOATtoDOUBLE(fSphereRadius))>=0) {

            // if the box is inside the sector
            if (itbsc->bsc_bspBSPTree.TestBox(boxdEntity)>=0) {
              // relate the entity to the sector
              if (en_RenderType==RT_BRUSH
                ||en_RenderType==RT_FIELDBRUSH
                ||en_RenderType==RT_TERRAIN) {  // brushes first
                AddRelationPairHeadHead(itbsc->bsc_rsEntities, en_rdSectors);
              } else {
                AddRelationPairTailTail(itbsc->bsc_rsEntities, en_rdSectors);
              }
            }
          }
        }
      }
    }
  }
}

void CEntity::FindSectorsAroundEntityNear(void)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // if not in spatial clasification
  if (en_fSpatialClassificationRadius<0) {
    // do nothing
    return;
  }
  // this may be called only for movable entities
  ASSERT(en_ulPhysicsFlags&EPF_MOVABLE);
  CMovableEntity *pen = (CMovableEntity *)this;

  // get bounding sphere and box of entity
  FLOAT fSphereRadius = en_fSpatialClassificationRadius;
  const FLOAT3D &vSphereCenter = en_plPlacement.pl_PositionVector;
  FLOATaabbox3D boxEntity(vSphereCenter, fSphereRadius);
  // make oriented bounding box of the entity
  FLOATobbox3D oboxEntity = FLOATobbox3D(en_boxSpatialClassification, 
    en_plPlacement.pl_PositionVector, en_mRotation);
  DOUBLEobbox3D oboxdEntity = FLOATtoDOUBLE(oboxEntity);

  CListHead lhActive;
  // for each sector around this entity
  {FOREACHSRCOFDST(en_rdSectors, CBrushSector, bsc_rsEntities, pbsc)
    // remember its link
    pbsc->bsc_prlLink = pbsc_iter;
    // add it to list of active sectors
    lhActive.AddTail(pbsc->bsc_lnInActiveSectors);
  ENDFOR}

  CStaticStackArray<CBrushPolygon *> &apbpo = pen->en_apbpoNearPolygons;
  // for each cached polygon
  for(INDEX iPolygon=0; iPolygon<apbpo.Count(); iPolygon++) {
    CBrushSector *pbsc = apbpo[iPolygon]->bpo_pbscSector;
    // add its sector if not already added, and has BSP (is zoning)
    if (!pbsc->bsc_lnInActiveSectors.IsLinked() && pbsc->bsc_bspBSPTree.bt_pbnRoot!=NULL) {
      lhActive.AddTail(pbsc->bsc_lnInActiveSectors);
      pbsc->bsc_prlLink = NULL;
    }
  }

  // for each active sector
  FOREACHINLIST(CBrushSector, bsc_lnInActiveSectors, lhActive, itbsc) {
    CBrushSector *pbsc = itbsc;
    // test if entity is in sector
    BOOL bIn =
        // the sector's bounding box has contact with given bounding box,
        (pbsc->bsc_boxBoundingBox.HasContactWith(boxEntity))&&
        // the sphere is inside the sector
        (pbsc->bsc_bspBSPTree.TestSphere(
              FLOATtoDOUBLE(vSphereCenter), FLOATtoDOUBLE(fSphereRadius))>=0)&&
        // (use more detailed testing for moving brushes)
        (en_RenderType!=RT_BRUSH||
          // oriented box touches box of sector
          (oboxEntity.HasContactWith(FLOATobbox3D(pbsc->bsc_boxBoundingBox)))&&
          // oriented box is in bsp
          (pbsc->bsc_bspBSPTree.TestBox(oboxdEntity)>=0));
    // if it is not
    if (!bIn) {
      // if it has link
      if (pbsc->bsc_prlLink!=NULL) {
        // remove link to that sector
        delete pbsc->bsc_prlLink;
        pbsc->bsc_prlLink = NULL;
      }
    // if it is
    } else {
      // if it doesn't have link
      if (pbsc->bsc_prlLink==NULL) {
        // add the link
        if (en_RenderType==RT_BRUSH
          ||en_RenderType==RT_FIELDBRUSH
          ||en_RenderType==RT_TERRAIN) {  // brushes first
          AddRelationPairHeadHead(pbsc->bsc_rsEntities, en_rdSectors);
        } else {
          AddRelationPairTailTail(pbsc->bsc_rsEntities, en_rdSectors);
        }
      }
    }
  }

  // clear list of active sectors
  {FORDELETELIST(CBrushSector, bsc_lnInActiveSectors, lhActive, itbsc) {
    itbsc->bsc_prlLink = NULL;
    itbsc->bsc_lnInActiveSectors.Remove();
  }}
  ASSERT(lhActive.IsEmpty());

  // if there is no link found
  if (en_rdSectors.IsEmpty()) {
    // test with brute force algorithm
    FindSectorsAroundEntity();
  }
}

/*
 * Uncache shadows of each polygon that has given gradient index
 */
void CEntity::UncacheShadowsForGradient(INDEX iGradient)
{
  if(en_RenderType != CEntity::RT_BRUSH)
  {
    ASSERTALWAYS("Uncache shadows for gradient called on non-brush entity!");
    return;
  }

  // for all brush mips
  FOREACHINLIST(CBrushMip, bm_lnInBrush, en_pbrBrush->br_lhBrushMips, itbm)
  {
    // for all sectors in the mip
    {FOREACHINDYNAMICARRAY(itbm->bm_abscSectors, CBrushSector, itbsc)
    {
      // for all polygons in this sector
      {FOREACHINSTATICARRAY(itbsc->bsc_abpoPolygons, CBrushPolygon, itbpo)
      {
        // if the polygon has shadows
        if (itbpo->bpo_bppProperties.bpp_ubGradientType == iGradient)
        {
          // uncache shadows
          itbpo->bpo_smShadowMap.Uncache();
        }
      }}
    }}
  }
}

/*
 * Get state transition for given state and event code.
 */
CEntity::pEventHandler CEntity::HandlerForStateAndEvent(SLONG slState, SLONG slEvent)
{
  // find translation in the translation table of the DLL class
  return en_pecClass->HandlerForStateAndEvent(slState, slEvent);
}

/*
 * Handle an event - return false if event was not handled.
 */
BOOL CEntity::HandleEvent(const CEntityEvent &ee)
{
  /*
   By default, base entities ignore all events.
   Events are handled by classes derived from CRationalEntity using state stack.

   Anyway, it is possible to override this in some class if some other way
   of event handling is desired.
   */

  return FALSE;
}

/////////////////////////////////////////////////////////////////////
// Event posting system

class CSentEvent {
public:
  CEntityPointer se_penEntity;
  CEntityEvent *se_peeEvent;
  inline void Clear(void) { se_penEntity = NULL; }
};

static CStaticStackArray<CSentEvent> _aseSentEvents;  // delayed events
/* Send an event to this entity. */
void CEntity::SendEvent(const CEntityEvent &ee)
{
  if (this==NULL) {
    ASSERT(FALSE);
    return;
  }
  CSentEvent &se = _aseSentEvents.Push();
  se.se_penEntity = this;
  se.se_peeEvent = ((CEntityEvent&)ee).MakeCopy();  // discard const qualifier
}

// find entities in a box (box must be around this entity)
void CEntity::FindEntitiesInRange(
  const FLOATaabbox3D &boxRange, CDynamicContainer<CEntity> &cen, BOOL bCollidingOnly)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // for each entity in the world of this entity
  FOREACHINDYNAMICCONTAINER(en_pwoWorld->wo_cenEntities, CEntity, iten) {
    // if it is zoning brush entity
    if (iten->en_RenderType == CEntity::RT_BRUSH && (iten->en_ulFlags&ENF_ZONING)) {
      // get first mip in its brush
      CBrushMip *pbm = iten->en_pbrBrush->GetFirstMip();
      // if the mip doesn't touch the box
      if (!pbm->bm_boxBoundingBox.HasContactWith(boxRange)) {
        // skip it
        continue;
      }

      // for all sectors in this mip
      FOREACHINDYNAMICARRAY(pbm->bm_abscSectors, CBrushSector, itbsc) {
        // if the sector doesn't touch the box
        if (!itbsc->bsc_boxBoundingBox.HasContactWith(boxRange)) {
          // skip it
          continue;
        }

        // for all entities in the sector
        {FOREACHDSTOFSRC(itbsc->bsc_rsEntities, CEntity, en_rdSectors, pen)
          // if the model entity touches the box
          if ((pen->en_RenderType==RT_MODEL || pen->en_RenderType==RT_EDITORMODEL)
            && boxRange.HasContactWith(
            FLOATaabbox3D(pen->GetPlacement().pl_PositionVector, pen->en_fSpatialClassificationRadius))) {

            // if it has collision box
            if (pen->en_pciCollisionInfo!=NULL) {
              // for each sphere
              FOREACHINSTATICARRAY(pen->en_pciCollisionInfo->ci_absSpheres, CMovingSphere, itms) {
                // project it
                itms->ms_vRelativeCenter0 = itms->ms_vCenter*pen->en_mRotation+pen->en_plPlacement.pl_PositionVector;
                // if the sphere touches the range
                if (boxRange.HasContactWith(FLOATaabbox3D(itms->ms_vRelativeCenter0, itms->ms_fR))) {
                  // add it to container
                  if (!cen.IsMember(pen)) {
                    cen.Add(pen);
                  }
                  goto next_entity;
                }
              }
            // if no collision box, but non-colliding are allowed
            } else if (!bCollidingOnly) {
              // add it to container
              if (!cen.IsMember(pen)) {
                cen.Add(pen);
              }
            }
          // if the brush entity touches the box
          } else if (pen->en_RenderType==RT_BRUSH && 
            boxRange.HasContactWith(
            FLOATaabbox3D(pen->GetPlacement().pl_PositionVector, pen->en_fSpatialClassificationRadius))) {
            // if the brush touches the box
            if (boxRange.HasContactWith(pen->en_pbrBrush->GetFirstMip()->bm_boxBoundingBox)) {
              // add it to container
              if (!cen.IsMember(pen)) {
                cen.Add(pen);
              }
            }
          } else if ((pen->en_RenderType==RT_SKAMODEL  || pen->en_RenderType==RT_SKAEDITORMODEL)
            && boxRange.HasContactWith(
            FLOATaabbox3D(pen->GetPlacement().pl_PositionVector, pen->en_fSpatialClassificationRadius))) {
            // if it has collision box
            if (pen->en_pciCollisionInfo!=NULL) {
              // for each sphere
              FOREACHINSTATICARRAY(pen->en_pciCollisionInfo->ci_absSpheres, CMovingSphere, itms) {
                // project it
                itms->ms_vRelativeCenter0 = itms->ms_vCenter*pen->en_mRotation+pen->en_plPlacement.pl_PositionVector;
                // if the sphere touches the range
                if (boxRange.HasContactWith(FLOATaabbox3D(itms->ms_vRelativeCenter0, itms->ms_fR))) {
                  // add it to container
                  if (!cen.IsMember(pen)) {
                    cen.Add(pen);
                  }
                  goto next_entity;
                }
              }
            // if no collision box, but non-colliding are allowed
            } else if (!bCollidingOnly) {
              // add it to container
              if (!cen.IsMember(pen)) {
                cen.Add(pen);
              }
            }
          }
          next_entity:;
        ENDFOR}
      }
    }
  }
}

/* Send an event to all entities in a box (box must be around this entity). */
void CEntity::SendEventInRange(const CEntityEvent &ee, const FLOATaabbox3D &boxRange)
{
  // find entities in the range
  CDynamicContainer<CEntity> cenToReceive;
  FindEntitiesInRange(boxRange, cenToReceive, FALSE);

  // for each entity in container
  FOREACHINDYNAMICCONTAINER(cenToReceive, CEntity, iten) {
    // send the event to it
    iten->SendEvent(ee);
  }
}

/* Handle all sent events. */
void CEntity::HandleSentEvents(void)
{
  CSetFPUPrecision FPUPrecision(FPT_24BIT);

  // while there are any unhandled events
  INDEX iFirstEvent = 0;
  while (iFirstEvent<_aseSentEvents.Count()) {
    CSentEvent &se = _aseSentEvents[iFirstEvent];
    // if not allowed to execute now
    if (!se.se_penEntity->IsAllowedForPrediction()) {
      // skip it
      iFirstEvent++;
      continue;
    }
    // if the entity is not destroyed
    if (!(se.se_penEntity->en_ulFlags&ENF_DELETED)) {
      // handle the current event
      se.se_penEntity->HandleEvent(*se.se_peeEvent);
    }
    // go to next event
    iFirstEvent++;
  }

  // for each event
  for(INDEX iee=0; iee<_aseSentEvents.Count(); iee++) {
    CSentEvent &se = _aseSentEvents[iee];
    // release the entity and destroy the event
    se.se_penEntity = NULL;
    delete se.se_peeEvent;
    se.se_peeEvent = NULL;
  }

  // flush all events
  _aseSentEvents.PopAll();
}

/////////////////////////////////////////////////////////////////////
// DLL class interface

/* Initialize for being virtual entity that is not rendered. */
void CEntity::InitAsVoid(void)
{
  en_RenderType = RT_VOID;
  en_pbrBrush = NULL;
}
/*
 * Initialize for beeing a model object.
 */
void CEntity::InitAsModel(void)
{
  // set render type to model
  en_RenderType = RT_MODEL;
  // create a model object
  en_pmoModelObject = new CModelObject;
  en_psiShadingInfo = new CShadingInfo;
  en_ulFlags &= ~ENF_VALIDSHADINGINFO;
}
/*
 * Initialize for beeing a ska model object.
 */
void CEntity::InitAsSkaModel(void)
{
  en_RenderType = RT_SKAMODEL;
  en_psiShadingInfo = new CShadingInfo;
  en_ulFlags &= ~ENF_VALIDSHADINGINFO;
}

/*
 * Initialize for beeing a terrain object.
 */
void CEntity::InitAsTerrain(void)
{
  en_RenderType = RT_TERRAIN;
  // if there is no existing terrain
  if(en_ptrTerrain == NULL) {
    // create a new empty terrain in the brush archive of current world
    en_ptrTerrain = en_pwoWorld->wo_taTerrains.ta_atrTerrains.New();
    en_ptrTerrain->tr_penEntity = this;

    // Create empty terrain
    en_ptrTerrain->CreateEmptyTerrain_t(257,257);
    en_ptrTerrain->SetTerrainSize(FLOAT3D(256,50,256));
    en_ptrTerrain->SetShadowMapsSize(0,0);
    en_ptrTerrain->UpdateShadowMap();
  }
  UpdateSpatialRange();
}

/*
 * Initialize for beeing a model object, for editor only.
 */
void CEntity::InitAsEditorModel(void)
{
  // set render type to model
  en_RenderType = RT_EDITORMODEL;
  // create a model object
  en_pmoModelObject = new CModelObject;
  en_psiShadingInfo = new CShadingInfo;
  en_ulFlags &= ~ENF_VALIDSHADINGINFO;
}
/*
 * Initialize for beeing a ska model object, for editor only.
 */
void CEntity::InitAsSkaEditorModel(void)
{
  // set render type to model
  en_RenderType = RT_SKAEDITORMODEL;
  // create a model object
  en_psiShadingInfo = new CShadingInfo;
  en_ulFlags &= ~ENF_VALIDSHADINGINFO;
}
/*
 * Initialize for beeing a brush object.
 */
void CEntity::InitAsBrush(void)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // set render type to brush
  en_RenderType = RT_BRUSH;
  // if there is no existing brush
  if (en_pbrBrush == NULL) {
    // create a new empty brush in the brush archive of current world
    en_pbrBrush = en_pwoWorld->wo_baBrushes.ba_abrBrushes.New();
    en_pbrBrush->br_penEntity = this;
    // create a brush mip for it
    CBrushMip *pbmMip = new CBrushMip;
    // add it to list
    en_pbrBrush->br_lhBrushMips.AddTail(pbmMip->bm_lnInBrush);
    // set back-pointer to the brush
    pbmMip->bm_pbrBrush = en_pbrBrush;
    en_pbrBrush->CalculateBoundingBoxes();
  }
  UpdateSpatialRange();
}

/*
 * Initialize for beeing a field brush object.
 */
void CEntity::InitAsFieldBrush(void)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // set render type to brush
  en_RenderType = RT_FIELDBRUSH;
  // if there is no existing brush
  if (en_pbrBrush == NULL) {
    // create a new empty brush in the brush archive of current world
    en_pbrBrush = en_pwoWorld->wo_baBrushes.ba_abrBrushes.New();
    en_pbrBrush->br_penEntity = this;
    // create a brush mip for it
    CBrushMip *pbmMip = new CBrushMip;
    // add it to list
    en_pbrBrush->br_lhBrushMips.AddTail(pbmMip->bm_lnInBrush);
    // set back-pointer to the brush
    pbmMip->bm_pbrBrush = en_pbrBrush;
    en_pbrBrush->CalculateBoundingBoxes();
  }
  UpdateSpatialRange();
}

/*
 *  Switch to Model/Editor model
 */
void CEntity::SwitchToModel(void)
{
  // change to editor model
  if(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL) {
    en_RenderType = RT_MODEL;
  } else if( en_RenderType==RT_SKAMODEL || en_RenderType==RT_SKAEDITORMODEL) {
    en_RenderType = RT_SKAMODEL;
  } else {
    // it must be model (not brush)
    ASSERT(FALSE);
  }
}
void CEntity::SwitchToEditorModel(void)
{
  // change to editor model
  if(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL) {
    en_RenderType = RT_EDITORMODEL;
  } else if( en_RenderType==RT_SKAMODEL || en_RenderType==RT_SKAEDITORMODEL) {
    en_RenderType = RT_SKAEDITORMODEL;
  } else {
    // it must be model (not brush)
    ASSERT(FALSE);
  }
}

/////////////////////////////////////////////////////////////////////
// Model manipulation
/* Set the model data for model entity. */
void CEntity::SetModel(const CTFileName &fnmModel)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  // try to
  try {
    // load the new model data
    en_pmoModelObject->SetData_t(fnmModel);
  // if failed
  } catch(char *strError) {
    (void)strError;
    DECLARE_CTFILENAME(fnmDefault, "Models\\Editor\\Axis.mdl");
    // try to
    try {
      // load the default model data
      en_pmoModelObject->SetData_t(fnmDefault);
    // if failed
    } catch(char *strErrorDefault) {
      FatalError(TRANS("Cannot load default model '%s':\n%s"),
        (CTString&)fnmDefault, strErrorDefault);
    }
  }
  UpdateSpatialRange();
  FindCollisionInfo();
}

void CEntity::SetModel(SLONG idModelComponent)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  CEntityComponent *pecModel = en_pecClass->ComponentForTypeAndID(
    ECT_MODEL, idModelComponent);
  en_pmoModelObject->SetData(pecModel->ec_pmdModel);
  UpdateSpatialRange();
  FindCollisionInfo();
}

void CEntity::SetSkaColisionInfo()
{
  ASSERT(en_RenderType==RT_SKAMODEL || en_RenderType==RT_SKAEDITORMODEL);
  // if there is no colision boxes for ska model 
  if(en_pmiModelInstance->mi_cbAABox.Count() == 0) {
    // add one default colision box
    en_pmiModelInstance->AddColisionBox("Default",FLOAT3D(-0.5,0,-0.5),FLOAT3D(0.5,2,0.5));
  }
  UpdateSpatialRange();
  FindCollisionInfo();
}

void CEntity::SetSkaModel_t(const CTString &fnmModel)
{
  ASSERT(en_RenderType==RT_SKAMODEL || en_RenderType==RT_SKAEDITORMODEL);
  // if model instance allready exists
  if(en_pmiModelInstance!=NULL) {
    // release it first
    en_pmiModelInstance->Clear();
  }
  try {
    // load the new model data
    en_pmiModelInstance = ParseSmcFile_t(fnmModel);
  } catch (char *strErrorDefault) {
    throw(strErrorDefault);
  }
  SetSkaColisionInfo();
}
BOOL CEntity::SetSkaModel(const CTString &fnmModel)
{
  ASSERT(en_RenderType==RT_SKAMODEL || en_RenderType==RT_SKAEDITORMODEL);
  // try to
  try {
    SetSkaModel_t(fnmModel);
  // if failed
  } catch(char *strError) {
    (void)strError;
    WarningMessage("%s\n\rLoading default model.\n", strError);
    DECLARE_CTFILENAME(fnmDefault, "Models\\Editor\\Ska\\Axis.smc");    
    // try to
    try {
      // load the default model data
      en_pmiModelInstance = ParseSmcFile_t(fnmDefault);
    // if failed
    } catch(char *strErrorDefault) {
      FatalError(TRANS("Cannot load default model '%s':\n%s"),
        (CTString&)fnmDefault, strErrorDefault);
    }
    // set colision info for default model
    SetSkaColisionInfo();
    return FALSE;
  }
  return TRUE;
}
// set/get model main blend color

void CEntity::SetModelColor( const COLOR colBlend)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  en_pmoModelObject->mo_colBlendColor = colBlend;
}

COLOR CEntity::GetModelColor(void) const
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  return en_pmoModelObject->mo_colBlendColor;
}


/* Get the model data for model entity. */

const CTFileName &CEntity::GetModel(void)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  return ((CAnimData*)en_pmoModelObject->GetData())->GetName();
}
/* Start new animation for model entity. */
void CEntity::StartModelAnim(INDEX iNewModelAnim, ULONG ulFlags)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  en_pmoModelObject->PlayAnim(iNewModelAnim, ulFlags);
}

/* Set the main texture data for model entity. */
void CEntity::SetModelMainTexture(const CTFileName &fnmTexture)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  // try to
  try {
    // load the texture data
    en_pmoModelObject->mo_toTexture.SetData_t(fnmTexture);

  // if failed
  } catch(char *strError) {
    (void)strError;
    DECLARE_CTFILENAME(fnmDefault, "Textures\\Editor\\Default.tex");
    // try to
    try {
      // load the default model data
      en_pmoModelObject->mo_toTexture.SetData_t(fnmDefault);
    // if failed
    } catch(char *strErrorDefault) {
      FatalError(TRANS("Cannot load default texture '%s':\n%s"),
        (CTString&)fnmDefault, strErrorDefault);
    }
  }
}
void CEntity::SetModelMainTexture(SLONG idTextureComponent)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  CEntityComponent *pecTexture = en_pecClass->ComponentForTypeAndID(
    ECT_TEXTURE, idTextureComponent);
  en_pmoModelObject->mo_toTexture.SetData(pecTexture->ec_ptdTexture);
}
/* Get the main texture data for model entity. */
const CTFileName &CEntity::GetModelMainTexture(void)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  return en_pmoModelObject->mo_toTexture.GetData()->GetName();
}
/* Start new animation for main texture of model entity. */
void CEntity::StartModelMainTextureAnim(INDEX iNewTextureAnim)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  en_pmoModelObject->mo_toTexture.StartAnim(iNewTextureAnim);
}

/* Set the reflection texture data for model entity. */
void CEntity::SetModelReflectionTexture(SLONG idTextureComponent)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  CEntityComponent *pecTexture = en_pecClass->ComponentForTypeAndID(
    ECT_TEXTURE, idTextureComponent);
  en_pmoModelObject->mo_toReflection.SetData(pecTexture->ec_ptdTexture);
}
/* Set the specular texture data for model entity. */
void CEntity::SetModelSpecularTexture(SLONG idTextureComponent)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  CEntityComponent *pecTexture = en_pecClass->ComponentForTypeAndID(
    ECT_TEXTURE, idTextureComponent);
  en_pmoModelObject->mo_toSpecular.SetData(pecTexture->ec_ptdTexture);
}

/* Add attachment to model */
void CEntity::AddAttachment(INDEX iAttachment, ULONG ulIDModel, ULONG ulIDTexture)
{
  // add attachment
  CModelObject &mo = en_pmoModelObject->AddAttachmentModel(iAttachment)->amo_moModelObject;
  // update model data
  CEntityComponent *pecWeaponModel = ComponentForTypeAndID( ECT_MODEL, ulIDModel);
  mo.SetData(pecWeaponModel->ec_pmdModel);
  // update texture data if different
  CEntityComponent *pecWeaponTexture = ComponentForTypeAndID( ECT_TEXTURE, ulIDTexture);
  mo.SetTextureData(pecWeaponTexture->ec_ptdTexture);
}
void CEntity::AddAttachment(INDEX iAttachment, CTFileName fnModel, CTFileName fnTexture)
{
  if( fnModel == CTString("")) return;
  CModelObject *pmo = GetModelObject();
  ASSERT( pmo != NULL);
  if( pmo == NULL) return;

  CAttachmentModelObject *pamo = pmo->AddAttachmentModel( iAttachment);
  try
  {
    pamo->amo_moModelObject.SetData_t( fnModel);
  }
  catch(char *strError)
  {
    (void) strError;
    pmo->RemoveAttachmentModel( iAttachment);
    return;
  }

  try
  {
    pamo->amo_moModelObject.mo_toTexture.SetData_t( fnTexture);
  }
  catch(char *strError)
  {
    (void) strError;
  }
}
/* Set the reflection texture data for attachment model entity. */
void CEntity::SetModelAttachmentReflectionTexture(INDEX iAttachment, SLONG idTextureComponent)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  CModelObject &mo = en_pmoModelObject->GetAttachmentModel(iAttachment)->amo_moModelObject;
  CEntityComponent *pecTexture = en_pecClass->ComponentForTypeAndID(
    ECT_TEXTURE, idTextureComponent);
  mo.mo_toReflection.SetData(pecTexture->ec_ptdTexture);
}
/* Set the specular texture data for attachment model entity. */
void CEntity::SetModelAttachmentSpecularTexture(INDEX iAttachment, SLONG idTextureComponent)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL);
  CModelObject &mo = en_pmoModelObject->GetAttachmentModel(iAttachment)->amo_moModelObject;
  CEntityComponent *pecTexture = en_pecClass->ComponentForTypeAndID(
    ECT_TEXTURE, idTextureComponent);
  mo.mo_toSpecular.SetData(pecTexture->ec_ptdTexture);
}

// Get all vertices of model entity in absolute space
void CEntity::GetModelVerticesAbsolute( CStaticStackArray<FLOAT3D> &avVertices, FLOAT fNormalOffset, FLOAT fMipFactor)
{
  ASSERT(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL ||
         en_RenderType==RT_SKAMODEL || en_RenderType==RT_SKAEDITORMODEL);
  // get placement
  CPlacement3D plPlacement = GetLerpedPlacement();
  // calculate rotation matrix
  FLOATmatrix3D mRotation;
  MakeRotationMatrixFast(mRotation, plPlacement.pl_OrientationAngle);
  if(en_RenderType==RT_MODEL || en_RenderType==RT_EDITORMODEL) {
    en_pmoModelObject->GetModelVertices( avVertices, mRotation, plPlacement.pl_PositionVector, fNormalOffset, fMipFactor);
  } else {
    GetModelInstance()->GetModelVertices( avVertices, mRotation, plPlacement.pl_PositionVector, fNormalOffset, fMipFactor);
  }
}

void EntityAdjustBonesCallback(void *pData)
{
  ((CEntity*)pData)->AdjustBones();
}
void EntityAdjustShaderParamsCallback(void *pData,INDEX iSurfaceID,CShader *pShader,ShaderParams &spParams)
{
  ((CEntity*)pData)->AdjustShaderParams(iSurfaceID,pShader,spParams);
}

// Returns true if bone exists and sets two given vectors as start and end point of specified bone
BOOL CEntity::GetBoneRelPosition(INDEX iBoneID, FLOAT3D &vStartPoint, FLOAT3D &vEndPoint)
{
  ASSERT(en_RenderType==RT_SKAMODEL || en_RenderType==RT_SKAEDITORMODEL);
  RM_SetObjectPlacement(CPlacement3D(FLOAT3D(0.0f,0.0f,0.0f),ANGLE3D(0.0f,0.0f,0.0f)));
  RM_SetBoneAdjustCallback(&EntityAdjustBonesCallback,this);
  return RM_GetBoneAbsPosition(*GetModelInstance(),iBoneID,vStartPoint,vEndPoint);
}

// Returns true if bone exists and sets two given vectors as start and end point of specified bone
BOOL CEntity::GetBoneAbsPosition(INDEX iBoneID, FLOAT3D &vStartPoint, FLOAT3D &vEndPoint)
{
  ASSERT(en_RenderType==RT_SKAMODEL || en_RenderType==RT_SKAEDITORMODEL);
  RM_SetObjectPlacement(GetLerpedPlacement());
  RM_SetBoneAdjustCallback(&EntityAdjustBonesCallback,this);
  return RM_GetBoneAbsPosition(*GetModelInstance(),iBoneID,vStartPoint,vEndPoint);
}

// Callback function for aditional bone adjustment
void CEntity::AdjustBones()
{
}

// Callback function for aditional shader params adjustment
void CEntity::AdjustShaderParams(INDEX iSurfaceID,CShader *pShader,ShaderParams &spParams)
{
}

// precache given component
void CEntity::PrecacheModel(SLONG slID)
{
  en_pecClass->ec_pdecDLLClass->PrecacheModel(slID);
}
void CEntity::PrecacheTexture(SLONG slID)
{
  en_pecClass->ec_pdecDLLClass->PrecacheTexture(slID);
}
void CEntity::PrecacheSound(SLONG slID)
{
  en_pecClass->ec_pdecDLLClass->PrecacheSound(slID);
}
void CEntity::PrecacheClass(SLONG slID, INDEX iUser /* = -1*/)
{
  en_pecClass->ec_pdecDLLClass->PrecacheClass(slID, iUser);
}

CAutoPrecacheSound::CAutoPrecacheSound()
{
  apc_psd = NULL;
}
CAutoPrecacheSound::~CAutoPrecacheSound()
{
  if (apc_psd!=NULL) {
    _pSoundStock->Release(apc_psd);
  }
}

void CAutoPrecacheSound::Precache(const CTFileName &fnm)
{
  if (apc_psd!=NULL) {
    _pSoundStock->Release(apc_psd);
  }
  try {
    if (fnm!="") {
      apc_psd = _pSoundStock->Obtain_t(fnm);
    }
  } catch (char *strError) {
    CPrintF("%s\n", strError);
  }
}

CAutoPrecacheModel::CAutoPrecacheModel()
{
  apc_pmd = NULL;
}
CAutoPrecacheModel::~CAutoPrecacheModel()
{
  if (apc_pmd!=NULL) {
    _pModelStock->Release(apc_pmd);
  }
}

void CAutoPrecacheModel::Precache(const CTFileName &fnm)
{
  if (apc_pmd!=NULL) {
    _pModelStock->Release(apc_pmd);
  }
  try {
    if (fnm!="") {
      apc_pmd = _pModelStock->Obtain_t(fnm);
    }
  } catch (char *strError) {
    CPrintF("%s\n", strError);
  }
}

CAutoPrecacheTexture::CAutoPrecacheTexture()
{
  apc_ptd = NULL;
}
CAutoPrecacheTexture::~CAutoPrecacheTexture()
{
  if (apc_ptd!=NULL) {
    _pTextureStock->Release(apc_ptd);
  }
}

void CAutoPrecacheTexture::Precache(const CTFileName &fnm)
{
  if (apc_ptd!=NULL) {
    _pTextureStock->Release(apc_ptd);
  }
  try {
    if (fnm!="") {
      apc_ptd = _pTextureStock->Obtain_t(fnm);
    }
  } catch (char *strError) {
    CPrintF("%s\n", strError);
  }
}

/* Get a filename for a component of given id. */
const CTFileName &CEntity::FileNameForComponent(SLONG slType, SLONG slID)
{
  // find the component
  CEntityComponent *pec = en_pecClass->ComponentForTypeAndID(
    (EntityComponentType)slType, slID);
  // the component must exist
  ASSERT(pec!=NULL);
  // get its name
  return pec->ec_fnmComponent;
}

// Get data for a texture component
CTextureData *CEntity::GetTextureDataForComponent(SLONG slID)
{
  CEntityComponent *pec = ComponentForTypeAndID( ECT_TEXTURE, slID);
  if (pec!=NULL) {
    return pec->ec_ptdTexture;
  } else {
    return NULL;
  }
}

// Get data for a model component
CModelData *CEntity::GetModelDataForComponent(SLONG slID)
{
  CEntityComponent *pec = ComponentForTypeAndID( ECT_MODEL, slID);
  if (pec!=NULL) {
    return pec->ec_pmdModel;
  } else {
    return NULL;
  }
}

/* Remove attachment from model */
void CEntity::RemoveAttachment(INDEX iAttachment)
{
  // remove attachment
  en_pmoModelObject->RemoveAttachmentModel(iAttachment);
}

/* Initialize last positions structure for particles. */
CLastPositions *CEntity::GetLastPositions(INDEX ctPositions)
{
  TIME tmNow = _pTimer->CurrentTick();
  if (en_plpLastPositions==NULL) {
    en_plpLastPositions = new CLastPositions;
    en_plpLastPositions->lp_avPositions.New(ctPositions);
    en_plpLastPositions->lp_ctUsed = 0;
    en_plpLastPositions->lp_iLast = 0;
    en_plpLastPositions->lp_tmLastAdded = tmNow;
    const FLOAT3D &vNow = GetPlacement().pl_PositionVector;
    for(INDEX iPos = 0; iPos<ctPositions; iPos++) {
      en_plpLastPositions->lp_avPositions[iPos] = vNow;
    }
  }

  while(en_plpLastPositions->lp_tmLastAdded<tmNow) {
    en_plpLastPositions->AddPosition(en_plpLastPositions->GetPosition(0));
  }

  return en_plpLastPositions;
}


/* Get absolute position of point on entity given relative to its size. */
void CEntity::GetEntityPointRatio(const FLOAT3D &vRatio, FLOAT3D &vAbsPoint, BOOL bLerped)
{
  ASSERT(bLerped || GetFPUPrecision()==FPT_24BIT);

  if (en_RenderType!=RT_MODEL && en_RenderType!=RT_EDITORMODEL &&
      en_RenderType!=RT_SKAMODEL && en_RenderType!=RT_SKAEDITORMODEL && 
      en_RenderType!=RT_BRUSH)  {
    ASSERT(FALSE);
    vAbsPoint = en_plPlacement.pl_PositionVector;
    return;
  }

  FLOAT3D vMin, vMax;

  if (en_RenderType==RT_BRUSH)
  {
    CBrushMip *pbmMip = en_pbrBrush->GetFirstMip();
    vMin = pbmMip->bm_boxBoundingBox.Min();
    vMax = pbmMip->bm_boxBoundingBox.Max();
    FLOAT3D vOff = vMax-vMin;
    vOff(1) *= vRatio(1);
    vOff(2) *= vRatio(2);
    vOff(3) *= vRatio(3);
    vAbsPoint = vMin+vOff;
  }
  else
  {
    if (_pNetwork->ga_ulDemoMinorVersion<=2) {
      vMin = en_pmoModelObject->GetCollisionBoxMin(GetCollisionBoxIndex());
      vMax = en_pmoModelObject->GetCollisionBoxMax(GetCollisionBoxIndex());
    } else {
      INDEX iEq;
      FLOATaabbox3D box;
      GetCollisionBoxParameters(GetCollisionBoxIndex(), box, iEq);
      vMin = box.Min();
      vMax = box.Max();
    }
    FLOAT3D vOff = vMax-vMin;
    vOff(1) *= vRatio(1);
    vOff(2) *= vRatio(2);
    vOff(3) *= vRatio(3);
    FLOAT3D vPos = vMin+vOff;
    if( bLerped)
    {
      CPlacement3D plLerped=GetLerpedPlacement();
      FLOATmatrix3D mRot;
      MakeRotationMatrixFast(mRot, plLerped.pl_OrientationAngle);
      vAbsPoint=plLerped.pl_PositionVector+vPos*mRot;
    }
    else
    {
      vAbsPoint = en_plPlacement.pl_PositionVector+vPos*en_mRotation;
    }
  }
}

/* Get absolute position of point on entity given in meters. */
void CEntity::GetEntityPointFixed(const FLOAT3D &vFixed, FLOAT3D &vAbsPoint)
{
  vAbsPoint = en_plPlacement.pl_PositionVector+vFixed*en_mRotation;
}
/* Get sector that given point is in - point must be inside this entity. */
CBrushSector *CEntity::GetSectorFromPoint(const FLOAT3D &vPointAbs)
{
  // for each sector around entity
  {FOREACHSRCOFDST(en_rdSectors, CBrushSector, bsc_rsEntities, pbsc)
    // if point is in this sector
    if( pbsc->bsc_bspBSPTree.TestSphere(FLOATtoDOUBLE(vPointAbs), 0.01)>=0) {
      // return that
      return pbsc;
    }
  ENDFOR;}
  return NULL;
}

/* Model change notify */
void CEntity::ModelChangeNotify(void)
{
  // if this is ska model
  if(en_RenderType == RT_SKAMODEL || en_RenderType == RT_SKAEDITORMODEL) {
    if(GetModelInstance()==NULL) {
      return;
    }

  // this is old model
  } else {
    if (en_pmoModelObject==NULL || en_pmoModelObject->GetData()==NULL) {
      return;
    }
  }

  UpdateSpatialRange();
  FindCollisionInfo();
}

void CEntity::TerrainChangeNotify(void)
{
//  GetTerrain()->RemoveLayer(0,FALSE);
//  GetTerrain()->AddDefaultLayer_t();
  GetTerrain()->ReBuildTerrain(TRUE);
  UpdateSpatialRange();
}

// map world polygon to/from indices
CBrushPolygon *CEntity::GetWorldPolygonPointer(INDEX ibpo)
{
  if (ibpo==-1) {
    return NULL;
  } else {
    return en_pwoWorld->wo_baBrushes.ba_apbpo[ibpo];
  }
}
INDEX CEntity::GetWorldPolygonIndex(CBrushPolygon *pbpo)
{
  if (pbpo==NULL) {
    return -1;
  } else {
    return pbpo->bpo_iInWorld;
  }
}

/////////////////////////////////////////////////////////////////////
// Sound functions
void CEntity::PlaySound(CSoundObject &so, SLONG idSoundComponent, SLONG slPlayType)
{
  CEntityComponent *pecSound = en_pecClass->ComponentForTypeAndID(ECT_SOUND, idSoundComponent);
  //so.Stop();
  so.Play(pecSound->ec_psdSound, slPlayType);
}

double CEntity::GetSoundLength(SLONG idSoundComponent)
{
  CEntityComponent *pecSound = en_pecClass->ComponentForTypeAndID(ECT_SOUND, idSoundComponent);
  return pecSound->ec_psdSound->GetSecondsLength();
}

void CEntity::PlaySound(CSoundObject &so, const CTFileName &fnmSound, SLONG slPlayType)
{
  // try to
  try {
    // load the sound data
    //so.Stop();
    so.Play_t(fnmSound, slPlayType);
  // if failed
  } catch(char *strError) {
    (void)strError;
    DECLARE_CTFILENAME(fnmDefault, "Sounds\\Default.wav");
    // try to
    try {
      // load the default sound data
      so.Play_t(fnmDefault, slPlayType);
    // if failed
    } catch(char *strErrorDefault) {
      FatalError(TRANS("Cannot load default sound '%s':\n%s"),
        (CTString&)fnmDefault, strErrorDefault);
    }
  }
}

/////////////////////////////////////////////////////////////////////
/*
 * Apply some damage directly to one entity.
 */
void CEntity::InflictDirectDamage(CEntity *penToDamage, CEntity *penInflictor, enum DamageType dmtType,
  FLOAT fDamageAmmount, const FLOAT3D &vHitPoint, const FLOAT3D &vDirection)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // if any of the entities are not allowed to execute now
  if (!IsAllowedForPrediction()
    ||!penToDamage->IsAllowedForPrediction()
    ||!penInflictor->IsAllowedForPrediction()) {
    // do nothing
    return;
  }

  // if significant damage
  if (fDamageAmmount>0) {
    penToDamage->ReceiveDamage(penInflictor, dmtType, fDamageAmmount, vHitPoint, vDirection);
  }
}

// find intensity of current entity at given distance
static inline FLOAT IntensityAtDistance(
  FLOAT fDamageAmmount, FLOAT fHotSpotRange, FLOAT fFallOffRange, FLOAT fDistance)
{
  // if further than fall-off range
  if (fDistance>fFallOffRange) {
    // intensity is zero
    return 0;
  // if closer than hot-spot range
  } else if (fDistance<fHotSpotRange) {
    // intensity is maximum
    return fDamageAmmount;
  // if between fall-off and hot-spot range
  } else {
    // interpolate
    return fDamageAmmount*(fFallOffRange-fDistance)/(fFallOffRange-fHotSpotRange);
  }
}

// check if a range damage can hit given model entity
static BOOL CheckModelRangeDamage(
  CEntity &en, const FLOAT3D &vCenter, FLOAT &fMinD, FLOAT3D &vHitPos)
{
  CCollisionInfo *pci = en.en_pciCollisionInfo;
  if (pci==NULL) {
    return FALSE;
  }

  // create 3 points along entity
  const FLOATmatrix3D &mR = en.en_mRotation;
  const FLOAT3D &vO = en.en_plPlacement.pl_PositionVector;
  FLOAT3D avPoints[3];
  INDEX ctSpheres = pci->ci_absSpheres.Count();
  if (ctSpheres<1) {
    return FALSE;
  }
  avPoints[1] = pci->ci_absSpheres[ctSpheres-1].ms_vCenter*mR+vO;
  avPoints[2] = pci->ci_absSpheres[0].ms_vCenter*mR+vO;
  avPoints[0] = (avPoints[1]+avPoints[2])*0.5f;

  // check if any point can be hit
  BOOL bCanHit = FALSE;
  for(INDEX i=0; i<3; i++) {
    CCastRay crRay( &en, avPoints[i], vCenter);
    crRay.cr_ttHitModels = CCastRay::TT_NONE;     // only brushes block the damage
    crRay.cr_bHitTranslucentPortals = FALSE;
    crRay.cr_bPhysical = TRUE;
    en.en_pwoWorld->CastRay(crRay);
    if (crRay.cr_penHit==NULL) {
      bCanHit = TRUE;
      break;
    }
  }

  // if none can be hit
  if (!bCanHit) {
    // skip this entity
    return FALSE;
  }

  // find minimum distance
  fMinD = UpperLimit(0.0f);
  vHitPos = vO;
  // for each sphere
  FOREACHINSTATICARRAY(pci->ci_absSpheres, CMovingSphere, itms) {
    // project it
    itms->ms_vRelativeCenter0 = itms->ms_vCenter*en.en_mRotation+vO;
    FLOAT fD = (itms->ms_vRelativeCenter0-vCenter).Length()-itms->ms_fR;
    if (fD<fMinD) {
      fMinD = Min(fD, fMinD);
      vHitPos = itms->ms_vRelativeCenter0;
    }
  }
  if (fMinD<0) {
    fMinD = 0;
  }
  return TRUE;
}

// check if a range damage can hit given brush entity
static BOOL CheckBrushRangeDamage(
  CEntity &en, const FLOAT3D &vCenter, FLOAT &fMinD, FLOAT3D &vHitPos)
{
  // don't actually check for brushes, doesn't have to be too exact
  const FLOAT3D &vO = en.en_plPlacement.pl_PositionVector;
  fMinD = (vO-vCenter).Length();
  vHitPos = vO;
  return TRUE;
}

/* Apply some damage to all entities in some range. */
void CEntity::InflictRangeDamage(CEntity *penInflictor, enum DamageType dmtType,
  FLOAT fDamageAmmount, const FLOAT3D &vCenter, FLOAT fHotSpotRange, FLOAT fFallOffRange)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // if any of the entities are not allowed to execute now
  if (!IsAllowedForPrediction()
    ||!penInflictor->IsAllowedForPrediction()) {
    // do nothing
    return;
  }

  // find entities in the range
  CDynamicContainer<CEntity> cenInRange;
  FindEntitiesInRange(FLOATaabbox3D(vCenter, fFallOffRange), cenInRange, TRUE);

  // for each entity in container
  FOREACHINDYNAMICCONTAINER(cenInRange, CEntity, iten) {
    CEntity &en = *iten;
    // if entity is not allowed to execute now
    if (!en.IsAllowedForPrediction()) {
      // do nothing
      continue;
    }

    // if can be hit
    FLOAT3D vHitPos;
    FLOAT fMinD;
    if (
      (en.en_RenderType==RT_MODEL || en.en_RenderType==RT_EDITORMODEL || 
        en.en_RenderType==RT_SKAMODEL || en.en_RenderType==RT_SKAEDITORMODEL )&&
       CheckModelRangeDamage(en, vCenter, fMinD, vHitPos) ||
      (en.en_RenderType==RT_BRUSH)&&
       CheckBrushRangeDamage(en, vCenter, fMinD, vHitPos)) {

      // find damage ammount
      FLOAT fAmmount = IntensityAtDistance(fDamageAmmount, fHotSpotRange, fFallOffRange, fMinD);

      // if significant
      if (fAmmount>0) {
        // inflict damage to it
        en.ReceiveDamage(penInflictor, dmtType, fAmmount, vHitPos, (vHitPos-vCenter).Normalize());
      }
    }
  }
}

/* Apply some damage to all entities in a box (this doesn't test for obstacles). */
void CEntity::InflictBoxDamage(CEntity *penInflictor, enum DamageType dmtType,
  FLOAT fDamageAmmount, const FLOATaabbox3D &box)
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // if any of the entities are not allowed to execute now
  if (!IsAllowedForPrediction()
    ||!penInflictor->IsAllowedForPrediction()) {
    // do nothing
    return;
  }

  // find entities in the range
  CDynamicContainer<CEntity> cenInRange;
  FindEntitiesInRange(box, cenInRange, TRUE);

  // for each entity in container
  FOREACHINDYNAMICCONTAINER(cenInRange, CEntity, iten) {
    CEntity &en = *iten;
    //ASSERT(en.en_pciCollisionInfo!=NULL);  // assured by FindEntitiesInRange()
    if (en.en_pciCollisionInfo==NULL) {
      continue;
    }
    CCollisionInfo *pci = en.en_pciCollisionInfo;
    // if entity is not allowed to execute now
    if (!en.IsAllowedForPrediction()) {
      // do nothing
      continue;
    }

    // if significant damage
    if (fDamageAmmount>0) {
      // inflict damage to it
      en.ReceiveDamage(penInflictor, dmtType,
        fDamageAmmount, box.Center(), 
        (box.Center()-en.GetPlacement().pl_PositionVector).Normalize());
    }
  }
}

// notify engine that gravity defined by this entity has changed
void CEntity::NotifyGravityChanged(void)
{
  if (_pNetwork->ga_ulDemoMinorVersion<=2) {
    // for each entity in the world of this entity
    FOREACHINDYNAMICCONTAINER(en_pwoWorld->wo_cenEntities, CEntity, iten) {
      CEntity *pen = &*iten;
      // if movable
      if (pen->en_ulPhysicsFlags&EPF_MOVABLE) {
        CMovableEntity *pmen = (CMovableEntity*)pen;
        // if the gravity has changed
        // add to movers
        pmen->AddToMovers();
      }
    }
  } else {
    // for each zoning brush in the world of this entity
    FOREACHINDYNAMICCONTAINER(en_pwoWorld->wo_cenEntities, CEntity, iten) {
      CEntity *penBrush = &*iten;
      if (iten->en_RenderType != CEntity::RT_BRUSH || !(iten->en_ulFlags&ENF_ZONING)) {
        continue;
      }
      CBrush3D *pbr = penBrush->en_pbrBrush;
      // get first brush mip
      CBrushMip *pbm = pbr->GetFirstMip();
      // for each sector in the brush mip
      {FOREACHINDYNAMICARRAY(pbm->bm_abscSectors, CBrushSector, itbsc) {
        // if controlled by this entity
        if ( penBrush->GetForceController(itbsc->GetForceType()) == this ) {
          // for each entity in the sector
          {FOREACHDSTOFSRC(itbsc->bsc_rsEntities, CEntity, en_rdSectors, pen) {
            // if movable
            if (pen->en_ulPhysicsFlags&EPF_MOVABLE) {
              CMovableEntity *pmen = (CMovableEntity*)pen;
              // add to movers
              pmen->AddToMovers();
            }
          ENDFOR}}
        }
      }}
    }
  }
}

// notify engine that collision of this entity was changed
void CEntity::NotifyCollisionChanged(void)
{
  if (en_pciCollisionInfo==NULL) {
    return;
  }

  // find colliding entities near this one
  static CStaticStackArray<CEntity*> apenNearEntities;
  en_pwoWorld->FindEntitiesNearBox(en_pciCollisionInfo->ci_boxCurrent, apenNearEntities);
  
  // for each of the found entities
  {for(INDEX ienFound=0; ienFound<apenNearEntities.Count(); ienFound++) {
    CEntity &enToNear = *apenNearEntities[ienFound];

    // if movable
    if (enToNear.en_ulPhysicsFlags&EPF_MOVABLE) {
      // add to movers
      ((CMovableEntity*)&enToNear)->AddToMovers();
    }
  }}
  apenNearEntities.PopAll();
}

// apply some damage to the entity (see event EDamage for more info)
void CEntity::ReceiveDamage(CEntity *penInflictor, enum DamageType dmtType,
  FLOAT fDamageAmmount, const FLOAT3D &vHitPoint, const FLOAT3D &vDirection)
{
  CEntityPointer penThis = this;  // keep this entity alive during this function
  // just throw an event that you are damaged (base entities don't really have health)
  EDamage eDamage;
  eDamage.penInflictor = penInflictor;
  eDamage.vDirection   = vDirection;
  eDamage.vHitPoint    = vHitPoint;
  eDamage.fAmount      = fDamageAmmount;
  eDamage.dmtType      = dmtType;
  SendEvent(eDamage);
}

/* Receive item through event */
BOOL CEntity::ReceiveItem(const CEntityEvent &ee)
{
  return FALSE;
}

/* Get entity info */
void *CEntity::GetEntityInfo(void)
{
  return NULL;
};
/* Fill in entity statistics - for AI purposes only */
BOOL CEntity::FillEntityStatistics(struct EntityStats *pes)
{
  return FALSE;
}

/////////////////////////////////////////////////////////////////////
// Overrides from CSerial

/*
 * Read from stream.
 */
void CEntity::Read_t( CTStream *istr) // throw char *
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // read base class data from stream
  if (istr->PeekID_t()==CChunkID("ENT4")) { // entity v4
    istr->ExpectID_t("ENT4");
    ULONG ulID;
    SLONG slSize;
    (*istr)>>ulID>>slSize;    // skip id and size
    (*istr)>>(ULONG &)en_RenderType
           >>en_ulPhysicsFlags
           >>en_ulCollisionFlags
           >>en_ulSpawnFlags
           >>en_ulFlags;
    (*istr).Read_t(&en_mRotation, sizeof(en_mRotation));
  } else if (istr->PeekID_t()==CChunkID("ENT3")) { // entity v3
    istr->ExpectID_t("ENT3");
    (*istr)>>(ULONG &)en_RenderType
           >>en_ulPhysicsFlags
           >>en_ulCollisionFlags
           >>en_ulSpawnFlags
           >>en_ulFlags;
    (*istr).Read_t(&en_mRotation, sizeof(en_mRotation));
  } else if (istr->PeekID_t()==CChunkID("ENT2")) { // entity v2
    istr->ExpectID_t("ENT2");
    (*istr)>>(ULONG &)en_RenderType
           >>en_ulPhysicsFlags
           >>en_ulCollisionFlags
           >>en_ulSpawnFlags
           >>en_ulFlags;
  } else {
    (*istr)>>(ULONG &)en_RenderType
           >>en_ulPhysicsFlags
           >>en_ulCollisionFlags
           >>en_ulFlags;
  }

  // clear flags for selection and caching info
  en_ulFlags &= ~(ENF_SELECTED|ENF_INRENDERING|ENF_VALIDSHADINGINFO);
  en_psiShadingInfo = NULL;
  en_pciCollisionInfo = NULL;

  // if this is a brush
  if ( en_RenderType == RT_BRUSH || en_RenderType == RT_FIELDBRUSH) {
    // read brush index in world's brush archive
    INDEX iBrush;
    (*istr)>>iBrush;
    en_pbrBrush = &en_pwoWorld->wo_baBrushes.ba_abrBrushes[iBrush];
    en_pbrBrush->br_penEntity = this;
  // if this is a terrain
  } else if (en_RenderType == RT_TERRAIN) {
    // read terrain index in world's terrain archive
    INDEX iTerrain;
    (*istr)>>iTerrain;
    en_ptrTerrain = &en_pwoWorld->wo_taTerrains.ta_atrTerrains[iTerrain];
    en_ptrTerrain->tr_penEntity = this;
    // force terrain regeneration (regenerate tiles on next render)
    en_ptrTerrain->ReBuildTerrain(TRUE);

  // if this is a model
  } else if ( en_RenderType == RT_MODEL || en_RenderType == RT_EDITORMODEL) {
    // create a new model object
    en_pmoModelObject = new CModelObject;
    en_psiShadingInfo = new CShadingInfo;
    en_ulFlags &= ~ENF_VALIDSHADINGINFO;

    // read model
    ReadModelObject_t(*istr, *en_pmoModelObject);
  // if this is a ska model
  } else if( en_RenderType == RT_SKAMODEL || en_RenderType == RT_SKAEDITORMODEL) {
    en_pmiModelInstance = CreateModelInstance("Temp");
    en_psiShadingInfo = new CShadingInfo;
    en_ulFlags &= ~ENF_VALIDSHADINGINFO;

    ReadModelInstance_t(*istr, *GetModelInstance());
  // if this is a void
  } else if (en_RenderType == RT_VOID) {
    en_pbrBrush = NULL;
  }

  // if the entity has a parent
  if (istr->PeekID_t()==CChunkID("PART")) { // parent
    // read the parent pointer and relative offset
    istr->ExpectID_t("PART");
    INDEX iParent;
    *istr>>iParent;
    extern BOOL _bReadEntitiesByID;
    if (_bReadEntitiesByID) {
      en_penParent = en_pwoWorld->EntityFromID(iParent);
    } else {
      en_penParent = en_pwoWorld->wo_cenAllEntities.Pointer(iParent);
    }
    *istr>>en_plRelativeToParent;
    // link to parent
    en_penParent->en_lhChildren.AddTail(en_lnInParent);
  }

  // read the derived class properties from stream
  ReadProperties_t(*istr);

  // if it is a light source
  {CLightSource *pls = GetLightSource();
  if (pls!=NULL) {
    // read the light source layer list
    pls->ls_penEntity = this;
    pls->Read_t(istr);
  }}

  // if it is a field brush
  CFieldSettings *pfs = GetFieldSettings();
  if (pfs!=NULL) {
    // remember its field settings
    ASSERT(en_RenderType == RT_FIELDBRUSH);
    en_pbrBrush->br_pfsFieldSettings = pfs;
  }

  // if entity was predictable
  if (en_ulFlags&ENF_PREDICTABLE) {
    // restore that condition
    en_ulFlags&=~ENF_PREDICTABLE; // have to clear it to be able to set it back
    SetPredictable(TRUE);
  }
}

/*
 * Write to stream.
 */
void CEntity::Write_t( CTStream *ostr) // throw char *
{
  ASSERT(GetFPUPrecision()==FPT_24BIT);

  // write base class data to stream
  ostr->WriteID_t("ENT4");
  SLONG slSize = 0;
  (*ostr)<<en_ulID<<slSize;    // save id and keep space for size
  (*ostr)<<(ULONG &)en_RenderType
         <<en_ulPhysicsFlags
         <<en_ulCollisionFlags
         <<en_ulSpawnFlags
         <<en_ulFlags;
  (*ostr).Write_t(&en_mRotation, sizeof(en_mRotation));
  // if this is a brush
  if ( en_RenderType == RT_BRUSH || en_RenderType == RT_FIELDBRUSH) {
    // write brush index in world's brush archive
    (*ostr)<<en_pwoWorld->wo_baBrushes.ba_abrBrushes.Index(en_pbrBrush);
  // if this is a terrain
  } else if ( en_RenderType == RT_TERRAIN) {
    // write brush index in world's brush archive
    (*ostr)<<en_pwoWorld->wo_taTerrains.ta_atrTerrains.Index(en_ptrTerrain);
  // if this is a model
  } else if ( en_RenderType == RT_MODEL || en_RenderType == RT_EDITORMODEL) {
    // write model
    WriteModelObject_t(*ostr, *en_pmoModelObject);
  // if this is ska model
  } else if ( en_RenderType == RT_SKAMODEL || en_RenderType == RT_SKAEDITORMODEL) {
    // write ska model
    WriteModelInstance_t(*ostr, *GetModelInstance());
  // if this is a void
  } else if (en_RenderType == RT_VOID) {
    NOTHING;
  }

  // if the entity has a parent
  if (en_penParent!=NULL) {
    // write the parent pointer and relative offset
    ostr->WriteID_t("PART"); // parent
    INDEX iParent = en_penParent->en_ulID;
    *ostr<<iParent;
    *ostr<<en_plRelativeToParent;
  }

  // write the derived class properties to stream
  WriteProperties_t(*ostr);
  // if it is a light source
  {CLightSource *pls = GetLightSource();
  if (pls!=NULL) {
    // read the light source layer list
    pls->Write_t(ostr);
  }}
}


/* Precache components that might be needed. */
void CEntity::Precache(void)
{
  NOTHING;
}


void CEntity::ChecksumForSync(ULONG &ulCRC, INDEX iExtensiveSyncCheck)
{
  if (iExtensiveSyncCheck>0) {
    CRC_AddLONG(ulCRC, en_ulFlags&~
      (ENF_SELECTED|ENF_INRENDERING|ENF_VALIDSHADINGINFO|ENF_FOUNDINGRIDSEARCH|ENF_WILLBEPREDICTED|ENF_PREDICTABLE));
    CRC_AddLONG(ulCRC, en_ulPhysicsFlags);
    CRC_AddLONG(ulCRC, en_ulCollisionFlags);
    CRC_AddLONG(ulCRC, en_ctReferences);
  }
  CRC_AddLONG(ulCRC, en_RenderType);
  if (iExtensiveSyncCheck>0) {
    CRC_AddLONG(ulCRC, en_ulID);
    CRC_AddFLOAT(ulCRC, en_fSpatialClassificationRadius);
    CRC_AddFLOAT(ulCRC, en_plPlacement.pl_PositionVector(1));
    CRC_AddFLOAT(ulCRC, en_plPlacement.pl_PositionVector(2));
    CRC_AddFLOAT(ulCRC, en_plPlacement.pl_PositionVector(3));
    CRC_AddFLOAT(ulCRC, en_plPlacement.pl_OrientationAngle(1));
    CRC_AddFLOAT(ulCRC, en_plPlacement.pl_OrientationAngle(2));
    CRC_AddFLOAT(ulCRC, en_plPlacement.pl_OrientationAngle(3));

    CRC_AddBlock(ulCRC, (UBYTE*)(void*)&en_mRotation, sizeof(en_mRotation));
  } else {
    CRC_AddLONG(ulCRC, (int)en_plPlacement.pl_PositionVector(1));
    CRC_AddLONG(ulCRC, (int)en_plPlacement.pl_PositionVector(2));
    CRC_AddLONG(ulCRC, (int)en_plPlacement.pl_PositionVector(3));
  }
}

void CEntity::DumpSync_t(CTStream &strm, INDEX iExtensiveSyncCheck)  // throw char *
{
  strm.FPrintF_t("\n---- #%05d ($%05d)----------------\n", 
    en_pwoWorld->wo_cenAllEntities.Index(this), en_pwoWorld->wo_cenEntities.Index(this));
  if (en_ulFlags&ENF_DELETED) {
    strm.FPrintF_t("*** DELETED ***\n");
  }
  strm.FPrintF_t("class: '%s'\n", GetClass()->ec_pdecDLLClass->dec_strName);
  strm.FPrintF_t("name: '%s'\n", GetName());
  if (iExtensiveSyncCheck>0) {
    strm.FPrintF_t("en_ulFlags:          0x%08X\n", en_ulFlags&~
      (ENF_SELECTED|ENF_INRENDERING|ENF_VALIDSHADINGINFO|ENF_FOUNDINGRIDSEARCH|ENF_WILLBEPREDICTED|ENF_PREDICTABLE));
    strm.FPrintF_t("en_ulPhysicsFlags:   0x%08X\n", en_ulPhysicsFlags);
    strm.FPrintF_t("en_ulCollisionFlags: 0x%08X\n", en_ulCollisionFlags);
    strm.FPrintF_t("en_ctReferences: %d\n", en_ctReferences);
  }
  strm.FPrintF_t("en_RenderType: %d\n", en_RenderType);
  strm.FPrintF_t("en_ulID: 0x%08x\n", en_ulID);
  if (iExtensiveSyncCheck>0) {
    strm.FPrintF_t("en_fSpatialClassificationRadius: %g(%08x)\n", 
      en_fSpatialClassificationRadius, (ULONG&)en_fSpatialClassificationRadius);
  }
  strm.FPrintF_t("placement: %g,%g,%g : %g,%g,%g\n",
    en_plPlacement.pl_PositionVector(1),
    en_plPlacement.pl_PositionVector(2),
    en_plPlacement.pl_PositionVector(3),
    en_plPlacement.pl_OrientationAngle(1),
    en_plPlacement.pl_OrientationAngle(2),
    en_plPlacement.pl_OrientationAngle(3));
  if (iExtensiveSyncCheck>0) {
    strm.FPrintF_t("placement raw:\n %08X %08X %08X\n %08X %08X %08X\n",
      (ULONG&)en_plPlacement.pl_PositionVector(1),
      (ULONG&)en_plPlacement.pl_PositionVector(2),
      (ULONG&)en_plPlacement.pl_PositionVector(3),
      (ULONG&)en_plPlacement.pl_OrientationAngle(1),
      (ULONG&)en_plPlacement.pl_OrientationAngle(2),
      (ULONG&)en_plPlacement.pl_OrientationAngle(3));
    strm.FPrintF_t("matrix:\n %g %g %g\n %g %g %g\n %g %g %g\n",
      en_mRotation(1,1), en_mRotation(1,2), en_mRotation(1,3),
      en_mRotation(2,1), en_mRotation(2,2), en_mRotation(2,3),
      en_mRotation(3,1), en_mRotation(3,2), en_mRotation(3,3));
    strm.FPrintF_t("matrix raw:\n %08X %08X %08X\n %08X %08X %08X\n %08X %08X %08X\n",
      (ULONG&)en_mRotation(1,1), (ULONG&)en_mRotation(1,2), (ULONG&)en_mRotation(1,3),
      (ULONG&)en_mRotation(2,1), (ULONG&)en_mRotation(2,2), (ULONG&)en_mRotation(2,3),
      (ULONG&)en_mRotation(3,1), (ULONG&)en_mRotation(3,2), (ULONG&)en_mRotation(3,3));
    if( en_pciCollisionInfo == NULL) {
      strm.FPrintF_t("Collision info NULL\n");
    } else if (en_RenderType==RT_BRUSH || en_RenderType==RT_FIELDBRUSH) {
      strm.FPrintF_t("Collision info: Brush entity\n");
    } else {
      strm.FPrintF_t("Collision info:\n");
      strm.FPrintF_t("Min height, Max height: %g, %g\n",
        en_pciCollisionInfo->ci_fMinHeight, en_pciCollisionInfo->ci_fMaxHeight);
      strm.FPrintF_t("Handle Y, Handle R: %g, %g\n",
        en_pciCollisionInfo->ci_fHandleY, en_pciCollisionInfo->ci_fHandleR);

      strm.FPrintF_t("Handle Y, Handle R: %g, %g\n",
        en_pciCollisionInfo->ci_fHandleY, en_pciCollisionInfo->ci_fHandleR);
    
      DUMPVECTOR(en_pciCollisionInfo->ci_boxCurrent.Min());
      DUMPVECTOR(en_pciCollisionInfo->ci_boxCurrent.Max());
      DUMPLONG(en_pciCollisionInfo->ci_ulFlags);
    }
  }
}


// get a pseudo-random number (safe for network gaming)
ULONG CEntity::IRnd(void) 
{
  return ((_pNetwork->ga_sesSessionState.Rnd()>>(31-16))&0xFFFF);
}


FLOAT CEntity::FRnd(void)
{
  return ((_pNetwork->ga_sesSessionState.Rnd()>>(31-24))&0xFFFFFF)/FLOAT(0xFFFFFF);
}



// returns ammount of memory used by entity
SLONG CEntity::GetUsedMemory(void)
{
  // initial size
  SLONG slUsedMemory = sizeof(CEntity);

  // add relations
  slUsedMemory += en_rdSectors.Count() * sizeof(CRelationLnk);

  // add allocated memory for model type (if any)
  switch( en_RenderType) {
  case CEntity::RT_MODEL:
  case CEntity::RT_EDITORMODEL:
    slUsedMemory += en_pmoModelObject->GetUsedMemory();
    break;
  case CEntity::RT_SKAMODEL:
  case CEntity::RT_SKAEDITORMODEL:
    slUsedMemory += en_pmiModelInstance->GetUsedMemory();
  default:
    break;
  }

  // add shading info (if any)
  if( en_psiShadingInfo !=NULL) {
    slUsedMemory += sizeof(CShadingInfo);
  }
  // add collision info (if any)
  if( en_pciCollisionInfo!=NULL) {
    slUsedMemory += sizeof(CCollisionInfo) + (en_pciCollisionInfo->ci_absSpheres.sa_Count * sizeof(CMovingSphere));
  }
  // add last positions memory (if any)
  if( en_plpLastPositions!=NULL) {
    slUsedMemory += sizeof(CLastPositions) + (en_plpLastPositions->lp_avPositions.sa_Count * sizeof(FLOAT3D));
  }

  // done
  return slUsedMemory;
}



/* Get pointer to entity property from its packed identifier. */
class CEntityProperty *CEntity::PropertyForTypeAndID(ULONG ulType, ULONG ulID)
{
  return en_pecClass->PropertyForTypeAndID(ulType, ulID);
}

/* Get pointer to entity component from its packed identifier. */
class CEntityComponent *CEntity::ComponentForTypeAndID(ULONG ulType, ULONG ulID)
{
  return en_pecClass->ComponentForTypeAndID((enum EntityComponentType)ulType, ulID);
}

/* Get pointer to entity property from its name. */
class CEntityProperty *CEntity::PropertyForName(const CTString &strPropertyName)
{
  return en_pecClass->PropertyForName(strPropertyName);
}
 
/* Create a new entity of given class in this world. */
CEntity *CEntity::CreateEntity(const CPlacement3D &plPlacement, SLONG idClass)
{
  CEntityComponent *pecClassComponent = en_pecClass->ComponentForTypeAndID(
    ECT_CLASS, idClass);
  return en_pwoWorld->CreateEntity(plPlacement, pecClassComponent->ec_pecEntityClass);
}



/////////////////////////////////////////////////////////////////////
// CLiveEntity

/*
 * Constructor.
 */
CLiveEntity::CLiveEntity(void)
{
  en_fHealth = 0;
}

/* Copy entity from another entity of same class. */
void CLiveEntity::Copy(CEntity &enOther, ULONG ulFlags)
{
  CEntity::Copy(enOther, ulFlags);
  CLiveEntity *plenOther = (CLiveEntity *)(&enOther);
  en_fHealth = plenOther->en_fHealth;
}
/* Read from stream. */
void CLiveEntity::Read_t( CTStream *istr) // throw char *
{
  CEntity::Read_t(istr);
  (*istr)>>en_fHealth;
}
/* Write to stream. */
void CLiveEntity::Write_t( CTStream *ostr) // throw char *
{
  CEntity::Write_t(ostr);
  (*ostr)<<en_fHealth;
}

// apply some damage to the entity (see event EDamage for more info)
void CLiveEntity::ReceiveDamage(CEntity *penInflictor, enum DamageType dmtType,
  FLOAT fDamage, const FLOAT3D &vHitPoint, const FLOAT3D &vDirection)
{
  CEntityPointer penThis = this;  // keep this entity alive during this function

  // reduce your health
  en_fHealth-=fDamage;

  // throw an event that you are damaged
  EDamage eDamage;
  eDamage.penInflictor = penInflictor;
  eDamage.vDirection   = vDirection;
  eDamage.vHitPoint    = vHitPoint;
  eDamage.fAmount      = fDamage;
  eDamage.dmtType      = dmtType;
  SendEvent(eDamage);

  // if health reached zero
  if (en_fHealth<=0) {
    // throw an event that you have died
    EDeath eDeath;
    eDeath.eLastDamage = eDamage;
    SendEvent(eDeath);
  }
}
/////////////////////////////////////////////////////////////////////
// CRationalEntity

/*
 * Constructor.
 */
CRationalEntity::CRationalEntity(void)
{
}

/* Calculate physics for moving. */
void CRationalEntity::ClearMovingTemp(void)
{
}
void CRationalEntity::PreMoving(void)
{
}
void CRationalEntity::DoMoving(void)
{
}
void CRationalEntity::PostMoving(void)
{
}
// create a checksum value for sync-check
void CRationalEntity::ChecksumForSync(ULONG &ulCRC, INDEX iExtensiveSyncCheck)
{
  CEntity::ChecksumForSync(ulCRC, iExtensiveSyncCheck);

  if (iExtensiveSyncCheck>0) {
    CRC_AddFLOAT(ulCRC, en_timeTimer);
    CRC_AddLONG(ulCRC, en_stslStateStack.Count());
  }
  if (iExtensiveSyncCheck>0) {
  }
}

// dump sync data to text file
void CRationalEntity::DumpSync_t(CTStream &strm, INDEX iExtensiveSyncCheck)  // throw char *
{
  CEntity::DumpSync_t(strm, iExtensiveSyncCheck);
  if (iExtensiveSyncCheck>0) {
    strm.FPrintF_t("en_timeTimer:  %g(%08x)\n", en_timeTimer, (ULONG&)en_timeTimer);
    strm.FPrintF_t("en_stslStateStack.Count(): %d\n", en_stslStateStack.Count());
  }
  strm.FPrintF_t("en_fHealth:    %g(%08x)\n", en_fHealth, (ULONG&)en_fHealth);
}

/* Copy entity from another entity of same class. */
void CRationalEntity::Copy(CEntity &enOther, ULONG ulFlags)
{
  CLiveEntity::Copy(enOther, ulFlags);
  if (!(ulFlags&COPY_REINIT)) {
    CRationalEntity *prenOther = (CRationalEntity *)(&enOther);
    en_timeTimer = prenOther->en_timeTimer;
    en_stslStateStack = prenOther->en_stslStateStack;
    if (prenOther->en_lnInTimers.IsLinked()) {
      en_pwoWorld->AddTimer(this);
    }
  }
}
/* Read from stream. */
void CRationalEntity::Read_t( CTStream *istr) // throw char *
{
  CLiveEntity::Read_t(istr);
  (*istr)>>en_timeTimer;
  // if waiting for thinking
  if (en_timeTimer!=THINKTIME_NEVER) {
    // add to list of thinkers
    en_pwoWorld->AddTimer(this);
  }
  // read the state stack
  en_stslStateStack.Clear();
  en_stslStateStack.SetAllocationStep(STATESTACK_ALLOCATIONSTEP);
  INDEX ctStates;
  (*istr)>>ctStates;
  for (INDEX iState=0; iState<ctStates; iState++) {
    (*istr)>>en_stslStateStack.Push();
  }

}
/* Write to stream. */
void CRationalEntity::Write_t( CTStream *ostr) // throw char *
{
  CLiveEntity::Write_t(ostr);
  // if not currently waiting for thinking
  if (!en_lnInTimers.IsLinked()) {
    // set dummy thinking time as a flag for later loading
    en_timeTimer = THINKTIME_NEVER;
  }
  (*ostr)<<en_timeTimer;
  // write the state stack
  (*ostr)<<en_stslStateStack.Count();
  for(INDEX iState=0; iState<en_stslStateStack.Count(); iState++) {
    (*ostr)<<en_stslStateStack[iState];
  }
}

/*
 * Set next timer event to occur at given moment time.
 */
void CRationalEntity::SetTimerAt(TIME timeAbsolute)
{
  // must never set think back in time, except for special 'never' time
  ASSERTMSG(timeAbsolute>_pTimer->CurrentTick() ||
    timeAbsolute==THINKTIME_NEVER, "Do not SetThink() back in time!");
  // set the timer
  en_timeTimer = timeAbsolute;

  // add to world's list of timers if neccessary
  if (en_timeTimer != THINKTIME_NEVER) {
    en_pwoWorld->AddTimer(this);
  } else {
    if (en_lnInTimers.IsLinked()) {
      en_lnInTimers.Remove();
    }
  }
}

/*
 * Set next timer event to occur after given time has elapsed.
 */
void CRationalEntity::SetTimerAfter(TIME timeDelta)
{
  // set the execution for the moment that is that much ahead of the current tick
  SetTimerAt(_pTimer->CurrentTick()+timeDelta);
}

/* Cancel eventual pending timer. */
void CRationalEntity::UnsetTimer(void)
{
  en_timeTimer = THINKTIME_NEVER;
  if (en_lnInTimers.IsLinked()) {
    en_lnInTimers.Remove();
  }
}

/*
 * Unwind stack to a given state.
 */
void CRationalEntity::UnwindStack(SLONG slThisState)
{
  // for each state on the stack (from top to bottom)
  for(INDEX iStateInStack=en_stslStateStack.Count()-1; iStateInStack>=0; iStateInStack--) {
    // if it is the state
    if (en_stslStateStack[iStateInStack]==slThisState) {
      // unwind to it
      en_stslStateStack.PopUntil(iStateInStack);
      return;
    }
  }
  // the state must be on the stack
  ASSERTALWAYS("Unwinding to unexisting state!");
}

/*
 * Jump to a new state.
 */
void CRationalEntity::Jump(SLONG slThisState, SLONG slTargetState, BOOL bOverride, const CEntityEvent &eeInput)
{
  // unwind the stack to this state
  UnwindStack(slThisState);
  // set the new topmost state
  if (bOverride) {
    slTargetState = en_pecClass->ec_pdecDLLClass->GetOverridenState(slTargetState);
  }
  en_stslStateStack[en_stslStateStack.Count()-1] = slTargetState;
  // handle the given event in the new state
  HandleEvent(eeInput);
};
/*
 * Call a subautomaton.
 */
void CRationalEntity::Call(SLONG slThisState, SLONG slTargetState, BOOL bOverride, const CEntityEvent &eeInput)
{
  // unwind the stack to this state
  UnwindStack(slThisState);
  // push the new state to stack
  if (bOverride) {
    slTargetState = en_pecClass->ec_pdecDLLClass->GetOverridenState(slTargetState);
  }
  en_stslStateStack.Push() = slTargetState;
  // handle the given event in the new state
  HandleEvent(eeInput);
};
/*
 * Return from a subautomaton.
 */
void CRationalEntity::Return(SLONG slThisState, const CEntityEvent &eeReturn)
{
  // unwind the stack to this state
  UnwindStack(slThisState);
  // pop one state from the stack
  en_stslStateStack.PopUntil(en_stslStateStack.Count()-2);
  // handle the given event in the new topmost state
  HandleEvent(eeReturn);
};

// print stack to debug output
const char *CRationalEntity::PrintStackDebug(void)
{
  _RPT2(_CRT_WARN, "-- stack of '%s'@%gs\n", GetName(), _pTimer->CurrentTick());

  INDEX ctStates = en_stslStateStack.Count();
  for(INDEX iState=ctStates-1; iState>=0; iState--) {
    SLONG slState = en_stslStateStack[iState];
    _RPT2(_CRT_WARN, "0x%08x %s\n", slState, 
      en_pecClass->ec_pdecDLLClass->HandlerNameForState(slState));
  }
  _RPT0(_CRT_WARN, "----\n");
  return "ok";
}

/*
 * Handle an event - return false if event was not handled.
 */
BOOL CRationalEntity::HandleEvent(const CEntityEvent &ee)
{
  // for each state on the stack (from top to bottom)
  for(INDEX iStateInStack=en_stslStateStack.Count()-1; iStateInStack>=0; iStateInStack--) {
    // try to find a handler in that state
    pEventHandler pehHandler =
      HandlerForStateAndEvent(en_stslStateStack[iStateInStack], ee.ee_slEvent);
    // if there is a handler
    if (pehHandler!=NULL) {
      // call the function
      BOOL bHandled = (this->*pehHandler)(ee);
      // if the event was successfully handled
      if (bHandled) {
        // return that it was handled
        return TRUE;
      }
    }
  }

  // if no transition was found, the event was not handled
  return FALSE;
}

/*
 * Called after creating and setting its properties.
 */
void CRationalEntity::OnInitialize(const CEntityEvent &eeInput)
{
  // make sure entity doesn't destroy itself during intialization
  CEntityPointer penThis = this;

  // do not think
  en_timeTimer = THINKTIME_NEVER;
  if (en_lnInTimers.IsLinked()) {
    en_lnInTimers.Remove();
  }

  // initialize state stack
  en_stslStateStack.Clear();
  en_stslStateStack.SetAllocationStep(STATESTACK_ALLOCATIONSTEP);

  en_stslStateStack.Push() = 1;   // start state is always state with number 1

  // call the main function of the entity
  HandleEvent(eeInput);
}

/* Called before releasing entity. */
void CRationalEntity::OnEnd(void)
{
  // cancel eventual pending timer
  UnsetTimer();
}

