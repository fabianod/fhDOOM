#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_local.h"
#include "RenderProgram.h"
#include "ImmediateMode.h"
#include "RenderList.h"

idCVar r_pomEnabled("r_pomEnabled", "0", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_BOOL, "POM enabled or disabled");
idCVar r_pomMaxHeight("r_pomMaxHeight", "0.045", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_FLOAT, "maximum height for POM");
idCVar r_shading("r_shading", "0", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_INTEGER, "0 = Doom3 (Blinn-Phong), 1 = Phong");
idCVar r_specularExp("r_specularExp", "10", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_FLOAT, "exponent used for specularity");
idCVar r_specularScale("r_specularScale", "1", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_FLOAT, "scale specularity globally for all surfaces");
idCVar r_renderList("r_renderlist", "1", CVAR_ARCHIVE | CVAR_RENDERER | CVAR_BOOL, "use render list");

/*
=====================
RB_GLSL_BlendLight

=====================
*/
static void RB_GLSL_BlendLight(const drawSurf_t *surf) {
	const srfTriangles_t*  tri = surf->geo;

  if(backEnd.currentSpace != surf->space)
  {
	idPlane	lightProject[4];

	for (int i = 0; i < 4; i++) {
		R_GlobalPlaneToLocal(surf->space->modelMatrix, backEnd.vLight->lightProject[i], lightProject[i]);
	}

	fhRenderProgram::SetBumpMatrix(lightProject[0].ToVec4(), lightProject[1].ToVec4());
	fhRenderProgram::SetSpecularMatrix(lightProject[2].ToVec4(), idVec4());
	fhRenderProgram::SetDiffuseMatrix(lightProject[3].ToVec4(), idVec4());
  }

  // this gets used for both blend lights and shadow draws
  if (tri->ambientCache) {
    int offset = vertexCache.Bind(tri->ambientCache);
	GL_SetupVertexAttributes(fhVertexLayout::DrawPosOnly, offset);
  }
  else if (tri->shadowCache) {
    int offset = vertexCache.Bind(tri->shadowCache);
	GL_SetupVertexAttributes(fhVertexLayout::Shadow, offset);
  }

  RB_DrawElementsWithCounters(tri);
}


/*
=====================
RB_GLSL_BlendLight

Dual texture together the falloff and projection texture with a blend
mode to the framebuffer, instead of interacting with the surface texture
=====================
*/
void RB_GLSL_BlendLight(const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2) {
  const idMaterial	*lightShader;
  const shaderStage_t	*stage;
  int					i;
  const float	*regs;

  if (!drawSurfs) {
    return;
  }
  if (r_skipBlendLights.GetBool()) {
    return;
  }
  RB_LogComment("---------- RB_GLSL_BlendLight ----------\n");

  GL_UseProgram(blendLightProgram);

  lightShader = backEnd.vLight->lightShader;
  regs = backEnd.vLight->shaderRegisters;

  // texture 1 will get the falloff texture
  backEnd.vLight->falloffImage->Bind(1);

  for (i = 0; i < lightShader->GetNumStages(); i++) {
    stage = lightShader->GetStage(i);

    if (!regs[stage->conditionRegister]) {
      continue;
    }

    GL_State(GLS_DEPTHMASK | stage->drawStateBits | GLS_DEPTHFUNC_EQUAL);

    // texture 0 will get the projected texture
    stage->texture.image->Bind(0);

    // get the modulate values from the light, including alpha, unlike normal lights
    backEnd.lightColor[0] = regs[stage->color.registers[0]];
    backEnd.lightColor[1] = regs[stage->color.registers[1]];
    backEnd.lightColor[2] = regs[stage->color.registers[2]];
    backEnd.lightColor[3] = regs[stage->color.registers[3]];
	fhRenderProgram::SetDiffuseColor(idVec4(backEnd.lightColor));    

    RB_RenderDrawSurfChainWithFunction(drawSurfs, RB_GLSL_BlendLight);
    RB_RenderDrawSurfChainWithFunction(drawSurfs2, RB_GLSL_BlendLight);
  }
}


/*
==================
R_WobbleskyTexGen
==================
*/
static void R_CreateWobbleskyTexMatrix(const drawSurf_t *surf, float time, float matrix[16]) {
  
  const int *parms = surf->material->GetTexGenRegisters();

  float	wobbleDegrees = surf->shaderRegisters[parms[0]];
  float	wobbleSpeed = surf->shaderRegisters[parms[1]];
  float	rotateSpeed = surf->shaderRegisters[parms[2]];

  wobbleDegrees = wobbleDegrees * idMath::PI / 180;
  wobbleSpeed = wobbleSpeed * 2 * idMath::PI / 60;
  rotateSpeed = rotateSpeed * 2 * idMath::PI / 60;

  // very ad-hoc "wobble" transform  
  float	a = time * wobbleSpeed;
  float	s = sin(a) * sin(wobbleDegrees);
  float	c = cos(a) * sin(wobbleDegrees);
  float	z = cos(wobbleDegrees);

  idVec3	axis[3];

  axis[2][0] = c;
  axis[2][1] = s;
  axis[2][2] = z;

  axis[1][0] = -sin(a * 2) * sin(wobbleDegrees);
  axis[1][2] = -s * sin(wobbleDegrees);
  axis[1][1] = sqrt(1.0f - (axis[1][0] * axis[1][0] + axis[1][2] * axis[1][2]));

  // make the second vector exactly perpendicular to the first
  axis[1] -= (axis[2] * axis[1]) * axis[2];
  axis[1].Normalize();

  // construct the third with a cross
  axis[0].Cross(axis[1], axis[2]);

  // add the rotate
  s = sin(rotateSpeed * time);
  c = cos(rotateSpeed * time);
  
  matrix[0] = axis[0][0] * c + axis[1][0] * s;
  matrix[4] = axis[0][1] * c + axis[1][1] * s;
  matrix[8] = axis[0][2] * c + axis[1][2] * s;

  matrix[1] = axis[1][0] * c - axis[0][0] * s;
  matrix[5] = axis[1][1] * c - axis[0][1] * s;
  matrix[9] = axis[1][2] * c - axis[0][2] * s;

  matrix[2] = axis[2][0];
  matrix[6] = axis[2][1];
  matrix[10] = axis[2][2];

  matrix[3] = matrix[7] = matrix[11] = 0.0f;
  matrix[12] = matrix[13] = matrix[14] = 0.0f;
}

/*
===============
RB_RenderTriangleSurface

Sets texcoord and vertex pointers
===============
*/
static void RB_GLSL_RenderTriangleSurface(const srfTriangles_t *tri) {
  if (!tri->ambientCache) {
    RB_DrawElementsImmediate(tri);
    return;
  }

  auto offset = vertexCache.Bind(tri->ambientCache);

  GL_SetupVertexAttributes(fhVertexLayout::DrawPosOnly, offset);  
  RB_DrawElementsWithCounters(tri);
}

static idPlane	fogPlanes[4];

/*
=====================
RB_T_BasicFog

=====================
*/
static void RB_GLSL_BasicFog(const drawSurf_t *surf) {

	if(backEnd.currentSpace != surf->space)
	{
		idPlane	local;

		R_GlobalPlaneToLocal(surf->space->modelMatrix, fogPlanes[0], local);
		local[3] += 0.5;
		const idVec4 bumpMatrixS = local.ToVec4();    

		local[0] = local[1] = local[2] = 0; local[3] = 0.5;

		const idVec4 bumpMatrixT = local.ToVec4();
		fhRenderProgram::SetBumpMatrix(bumpMatrixS, bumpMatrixT);

		// GL_S is constant per viewer
		R_GlobalPlaneToLocal(surf->space->modelMatrix, fogPlanes[2], local);
		local[3] += FOG_ENTER;
		const idVec4 diffuseMatrixT = local.ToVec4();   
	
		R_GlobalPlaneToLocal(surf->space->modelMatrix, fogPlanes[3], local);
		const idVec4 diffuseMatrixS = local.ToVec4();

		fhRenderProgram::SetDiffuseMatrix(diffuseMatrixS, diffuseMatrixT);
	}

	RB_GLSL_RenderTriangleSurface(surf->geo);
}

/*
==================
R_GLSL_Init

Load default shaders.
==================
*/
void	R_GLSL_Init( void )
{
	fhRenderProgram::Init();
	fhImmediateMode::Init();
}


/*
==================
RB_GLSL_FogPass
==================
*/
void RB_GLSL_FogPass(const drawSurf_t *drawSurfs, const drawSurf_t *drawSurfs2) {
  assert(fogLightProgram);

  RB_LogComment("---------- RB_GLSL_FogPass ----------\n");  

  GL_UseProgram(fogLightProgram);

  // create a surface for the light frustom triangles, which are oriented drawn side out
  const srfTriangles_t* frustumTris = backEnd.vLight->frustumTris;

  // if we ran out of vertex cache memory, skip it
  if (!frustumTris->ambientCache) {
    return;
  }    
  
  drawSurf_t ds;
  memset(&ds, 0, sizeof(ds));
  ds.space = &backEnd.viewDef->worldSpace;
  ds.geo = frustumTris;
  ds.scissorRect = backEnd.viewDef->scissor;

  // find the current color and density of the fog
  const idMaterial *lightShader = backEnd.vLight->lightShader;
  const float	     *regs        = backEnd.vLight->shaderRegisters;
  // assume fog shaders have only a single stage
  const shaderStage_t	*stage = lightShader->GetStage(0);

  backEnd.lightColor[0] = regs[stage->color.registers[0]];
  backEnd.lightColor[1] = regs[stage->color.registers[1]];
  backEnd.lightColor[2] = regs[stage->color.registers[2]];
  backEnd.lightColor[3] = regs[stage->color.registers[3]];
  
  fhRenderProgram::SetDiffuseColor(idVec4(backEnd.lightColor));

  // calculate the falloff planes
  float	a;

  // if they left the default value on, set a fog distance of 500
  if (backEnd.lightColor[3] <= 1.0) {
    a = -0.5f / DEFAULT_FOG_DISTANCE;
  }
  else {
    // otherwise, distance = alpha color
    a = -0.5f / backEnd.lightColor[3];
  }  

  // texture 0 is the falloff image
  globalImages->fogImage->Bind(0);

  fogPlanes[0][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[2];
  fogPlanes[0][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[6];
  fogPlanes[0][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[10];
  fogPlanes[0][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[14];

  fogPlanes[1][0] = a * backEnd.viewDef->worldSpace.modelViewMatrix[0];
  fogPlanes[1][1] = a * backEnd.viewDef->worldSpace.modelViewMatrix[4];
  fogPlanes[1][2] = a * backEnd.viewDef->worldSpace.modelViewMatrix[8];
  fogPlanes[1][3] = a * backEnd.viewDef->worldSpace.modelViewMatrix[12];

  // texture 1 is the entering plane fade correction
  globalImages->fogEnterImage->Bind(1);

  // T will get a texgen for the fade plane, which is always the "top" plane on unrotated lights
  fogPlanes[2][0] = 0.001f * backEnd.vLight->fogPlane[0];
  fogPlanes[2][1] = 0.001f * backEnd.vLight->fogPlane[1];
  fogPlanes[2][2] = 0.001f * backEnd.vLight->fogPlane[2];
  fogPlanes[2][3] = 0.001f * backEnd.vLight->fogPlane[3];

  // S is based on the view origin
  float s = backEnd.viewDef->renderView.vieworg * fogPlanes[2].Normal() + fogPlanes[2][3];

  fogPlanes[3][0] = 0;
  fogPlanes[3][1] = 0;
  fogPlanes[3][2] = 0;
  fogPlanes[3][3] = FOG_ENTER + s;

  // draw it
  backEnd.glState.forceGlState = true;
  GL_State(GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL);
  RB_RenderDrawSurfChainWithFunction(drawSurfs, RB_GLSL_BasicFog);
  RB_RenderDrawSurfChainWithFunction(drawSurfs2, RB_GLSL_BasicFog);

  // the light frustum bounding planes aren't in the depth buffer, so use depthfunc_less instead
  // of depthfunc_equal
  GL_State(GLS_DEPTHMASK | GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_LESS);
  GL_Cull(CT_BACK_SIDED);
  RB_RenderDrawSurfChainWithFunction(&ds, RB_GLSL_BasicFog);
  GL_Cull(CT_FRONT_SIDED);
}




/*
==============================================================================

BACK END RENDERING OF STENCIL SHADOWS

==============================================================================
*/


/*
=====================
RB_GLSL_Shadow

the shadow volumes face INSIDE
=====================
*/

static void RB_GLSL_Shadow(const drawSurf_t *surf) {
  const srfTriangles_t	*tri;

  // set the light position if we are using a vertex program to project the rear surfaces
  if (surf->space != backEnd.currentSpace) {
    idVec4 localLight;

    R_GlobalPointToLocal(surf->space->modelMatrix, backEnd.vLight->globalLightOrigin, localLight.ToVec3());
    localLight.w = 0.0f;

    assert(shadowProgram);
    fhRenderProgram::SetLocalLightOrigin(localLight);
    fhRenderProgram::SetProjectionMatrix(backEnd.viewDef->projectionMatrix);
    fhRenderProgram::SetModelViewMatrix(surf->space->modelViewMatrix);
  }

  tri = surf->geo;

  if (!tri->shadowCache) {
    return;
  }

  const auto offset = vertexCache.Bind(tri->shadowCache);

  GL_SetupVertexAttributes(fhVertexLayout::Shadow, offset);
  //GL_SetVertexLayout(fhVertexLayout::Shadow);  
  //glVertexAttribPointer(fhRenderProgram::vertex_attrib_position_shadow, 4, GL_FLOAT, false, sizeof(shadowCache_t), attributeOffset(offset, 0));

  // we always draw the sil planes, but we may not need to draw the front or rear caps
  int	numIndexes;
  bool external = false;

  if (!r_useExternalShadows.GetInteger()) {
    numIndexes = tri->numIndexes;
  }
  else if (r_useExternalShadows.GetInteger() == 2) { // force to no caps for testing
    numIndexes = tri->numShadowIndexesNoCaps;
  }
  else if (!(surf->dsFlags & DSF_VIEW_INSIDE_SHADOW)) {
    // if we aren't inside the shadow projection, no caps are ever needed needed
    numIndexes = tri->numShadowIndexesNoCaps;
    external = true;
  }
  else if (!backEnd.vLight->viewInsideLight && !(surf->geo->shadowCapPlaneBits & SHADOW_CAP_INFINITE)) {
    // if we are inside the shadow projection, but outside the light, and drawing
    // a non-infinite shadow, we can skip some caps
    if (backEnd.vLight->viewSeesShadowPlaneBits & surf->geo->shadowCapPlaneBits) {
      // we can see through a rear cap, so we need to draw it, but we can skip the
      // caps on the actual surface
      numIndexes = tri->numShadowIndexesNoFrontCaps;
    }
    else {
      // we don't need to draw any caps
      numIndexes = tri->numShadowIndexesNoCaps;
    }
    external = true;
  }
  else {
    // must draw everything
    numIndexes = tri->numIndexes;
  }

  // set depth bounds
  if (glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool()) {
    glDepthBoundsEXT(surf->scissorRect.zmin, surf->scissorRect.zmax);
  }
/*
  // debug visualization
  if (r_showShadows.GetInteger()) {

    if (r_showShadows.GetInteger() == 3) {
      if (external) {
        glColor3f(0.1 / backEnd.overBright, 1 / backEnd.overBright, 0.1 / backEnd.overBright);
      }
      else {
        // these are the surfaces that require the reverse
        glColor3f(1 / backEnd.overBright, 0.1 / backEnd.overBright, 0.1 / backEnd.overBright);
      }
    }
    else {
      // draw different color for turboshadows
      if (surf->geo->shadowCapPlaneBits & SHADOW_CAP_INFINITE) {
        if (numIndexes == tri->numIndexes) {
          glColor3f(1 / backEnd.overBright, 0.1 / backEnd.overBright, 0.1 / backEnd.overBright);
        }
        else {
          glColor3f(1 / backEnd.overBright, 0.4 / backEnd.overBright, 0.1 / backEnd.overBright);
        }
      }
      else {
        if (numIndexes == tri->numIndexes) {
          glColor3f(0.1 / backEnd.overBright, 1 / backEnd.overBright, 0.1 / backEnd.overBright);
        }
        else if (numIndexes == tri->numShadowIndexesNoFrontCaps) {
          glColor3f(0.1 / backEnd.overBright, 1 / backEnd.overBright, 0.6 / backEnd.overBright);
        }
        else {
          glColor3f(0.6 / backEnd.overBright, 1 / backEnd.overBright, 0.1 / backEnd.overBright);
        }
      }
    }

    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glDisable(GL_STENCIL_TEST);
    GL_Cull(CT_TWO_SIDED);
    RB_DrawShadowElementsWithCounters(tri, numIndexes);
    GL_Cull(CT_FRONT_SIDED);
    glEnable(GL_STENCIL_TEST);    
    return;
  }
*/
  // patent-free work around
  if (!external) {
    // "preload" the stencil buffer with the number of volumes
    // that get clipped by the near or far clip plane
    glStencilOp(GL_KEEP, GL_DECR_WRAP, GL_DECR_WRAP);
    GL_Cull(CT_FRONT_SIDED);
    RB_DrawShadowElementsWithCounters(tri, numIndexes);
    glStencilOp(GL_KEEP, GL_INCR_WRAP, GL_INCR_WRAP);
    GL_Cull(CT_BACK_SIDED);
    RB_DrawShadowElementsWithCounters(tri, numIndexes);
  }

  // traditional depth-pass stencil shadows
  glStencilOp(GL_KEEP, GL_KEEP, GL_INCR_WRAP);
  GL_Cull(CT_FRONT_SIDED);
  RB_DrawShadowElementsWithCounters(tri, numIndexes);

  glStencilOp(GL_KEEP, GL_KEEP, GL_DECR_WRAP);
  GL_Cull(CT_BACK_SIDED);
  RB_DrawShadowElementsWithCounters(tri, numIndexes);
}


/*
=====================
RB_GLSL_StencilShadowPass

Stencil test should already be enabled, and the stencil buffer should have
been set to 128 on any surfaces that might receive shadows
=====================
*/



void RB_GLSL_StencilShadowPass(const drawSurf_t *drawSurfs) {
  assert(shadowProgram);

  if (backEnd.vLight->lightDef->ShadowMode() != shadowMode_t::StencilShadow) {
    return;
  }

  if (!drawSurfs) {
    return;
  }

  GL_UseProgram(shadowProgram);

  RB_LogComment("---------- RB_StencilShadowPass ----------\n");

  // for visualizing the shadows
  if (r_showShadows.GetInteger()) {
    if (r_showShadows.GetInteger() == 2) {
      // draw filled in
      GL_State(GLS_DEPTHMASK | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_LESS);
    }
    else {
      // draw as lines, filling the depth buffer
      GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_POLYMODE_LINE | GLS_DEPTHFUNC_ALWAYS);
    }
  }
  else {
    // don't write to the color buffer, just the stencil buffer
    GL_State(GLS_DEPTHMASK | GLS_COLORMASK | GLS_ALPHAMASK | GLS_DEPTHFUNC_LESS);
  }

  if (r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat()) {
    glPolygonOffset(r_shadowPolygonFactor.GetFloat(), -r_shadowPolygonOffset.GetFloat());
    glEnable(GL_POLYGON_OFFSET_FILL);
  }

  glStencilFunc(GL_ALWAYS, 1, 255);

  if (glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool()) {
    glEnable(GL_DEPTH_BOUNDS_TEST_EXT);
  }

  RB_RenderDrawSurfChainWithFunction(drawSurfs, RB_GLSL_Shadow);

  GL_Cull(CT_FRONT_SIDED);

  if (r_shadowPolygonFactor.GetFloat() || r_shadowPolygonOffset.GetFloat()) {
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  if (glConfig.depthBoundsTestAvailable && r_useDepthBoundsTest.GetBool()) {
    glDisable(GL_DEPTH_BOUNDS_TEST_EXT);
  }

  glStencilFunc(GL_GEQUAL, 128, 255);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}


/*
=====================
RB_GLSL_FillDepthBuffer

If we are rendering a subview with a near clip plane, use a second texture
to force the alpha test to fail when behind that clip plane
=====================
*/
void RB_GLSL_FillDepthBuffer(drawSurf_t **drawSurfs, int numDrawSurfs) {
	fhTimeElapsed timeElapsed( &backEnd.stats.groups[backEndGroup::DepthPrepass].time );
	backEnd.stats.groups[backEndGroup::DepthPrepass].passes += 1;

	DepthRenderList depthRenderList;
	depthRenderList.AddDrawSurfaces( drawSurfs, numDrawSurfs );
	depthRenderList.Submit();
}


/*
=====================
RB_GLSL_RenderSpecialShaderStage
=====================
*/
void RB_GLSL_RenderSpecialShaderStage(const float* regs, const shaderStage_t* pStage, glslShaderStage_t* glslStage, const drawSurf_t *surf) {  
	assert(surf);
	const srfTriangles_t* tri = surf->geo;
	GL_State(pStage->drawStateBits);
	GL_UseProgram(glslStage->program);

	const auto offset = vertexCache.Bind(tri->ambientCache);  
	GL_SetupVertexAttributes(fhVertexLayout::Draw, offset);

	fhRenderProgram::SetModelViewMatrix(surf->space->modelViewMatrix);	

	if (surf->space->modelDepthHack != 0.0f) {
		RB_EnterModelDepthHack( surf->space->modelDepthHack );
	}
	else if (surf->space->weaponDepthHack) {
		RB_EnterWeaponDepthHack();
	}
	else {
		fhRenderProgram::SetProjectionMatrix(backEnd.viewDef->projectionMatrix);
	}

	for (int i = 0; i < glslStage->numShaderParms; i++) {
		idVec4 parm;
		parm[0] = regs[glslStage->shaderParms[i][0]];
		parm[1] = regs[glslStage->shaderParms[i][1]];
		parm[2] = regs[glslStage->shaderParms[i][2]];
		parm[3] = regs[glslStage->shaderParms[i][3]];
		fhRenderProgram::SetShaderParm(i, parm);    
	}

	// current render
	const int	w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
	const int	h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;

	fhRenderProgram::SetCurrentRenderSize(
		idVec2(globalImages->currentRenderImage->uploadWidth, globalImages->currentRenderImage->uploadHeight),
		idVec2(w, h));

	// set textures
	for (int i = 0; i < glslStage->numShaderMaps; i++) {
		if (glslStage->shaderMap[i]) {
			glslStage->shaderMap[i]->Bind(i);
		}
	}

	// draw it
	RB_DrawElementsWithCounters(tri);

	if (surf->space->modelDepthHack != 0.0f || surf->space->weaponDepthHack) {
		RB_LeaveDepthHack();
	}
}



/*
=================
R_ReloadGlslPrograms_f
=================
*/
void R_ReloadGlslPrograms_f( const idCmdArgs &args ) {
	fhRenderProgram::ReloadAll();
}


/*
====================
RB_GLSL_RenderShaderStage
====================
*/
void RB_GLSL_RenderShaderStage(const drawSurf_t *surf, const shaderStage_t* pStage) {
  assert(defaultProgram);
  assert(skyboxProgram);
  assert(bumpyEnvProgram);

  // set the color  
  float color[4];
  color[0] = surf->shaderRegisters[pStage->color.registers[0]];
  color[1] = surf->shaderRegisters[pStage->color.registers[1]];
  color[2] = surf->shaderRegisters[pStage->color.registers[2]];
  color[3] = surf->shaderRegisters[pStage->color.registers[3]];

  // skip the entire stage if an add would be black
  if ((pStage->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE)
    && color[0] <= 0 && color[1] <= 0 && color[2] <= 0) {
    return;
  }

  // skip the entire stage if a blend would be completely transparent
  if ((pStage->drawStateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS)) == (GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA)
    && color[3] <= 0) {
    return;
  }
  
  const auto offset = vertexCache.Bind(surf->geo->ambientCache); 

  bool programWasReset = false;

  if (pStage->texture.texgen == TG_DIFFUSE_CUBE) {
    return;
  }
  else if (pStage->texture.texgen == TG_SKYBOX_CUBE || pStage->texture.texgen == TG_WOBBLESKY_CUBE) {
    programWasReset = GL_UseProgram(skyboxProgram);    

    idMat4 textureMatrix = mat4_identity;
    if (pStage->texture.texgen == TG_WOBBLESKY_CUBE) {
      R_CreateWobbleskyTexMatrix(surf, backEnd.viewDef->floatTime, textureMatrix.ToFloatPtr());
    }

    idVec4 localViewOrigin;
    R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3() );
    localViewOrigin[3] = 1.0f;
    fhRenderProgram::SetLocalViewOrigin(localViewOrigin);
	fhRenderProgram::SetTextureMatrix(textureMatrix.ToFloatPtr());    

	GL_SetupVertexAttributes(fhVertexLayout::DrawPosColorOnly, offset);
  }
  else if (pStage->texture.texgen == TG_SCREEN) {
    return;
  }
  else if (pStage->texture.texgen == TG_GLASSWARP) {
    return;
  }
  else if (pStage->texture.texgen == TG_REFLECT_CUBE) {

    programWasReset = GL_UseProgram(bumpyEnvProgram);

    idMat4 textureMatrix = mat4_identity;

    idVec4 localViewOrigin;
    R_GlobalPointToLocal(surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3());
    localViewOrigin[3] = 1.0f;
	fhRenderProgram::SetLocalViewOrigin(localViewOrigin);        
	fhRenderProgram::SetTextureMatrix(textureMatrix.ToFloatPtr());

	GL_SetupVertexAttributes(fhVertexLayout::Draw, offset);

    // set the texture matrix
    idVec4 textureMatrixST[2];
    textureMatrixST[0][0] = 1;
    textureMatrixST[0][1] = 0;
    textureMatrixST[0][2] = 0;
    textureMatrixST[0][3] = 0;
    textureMatrixST[1][0] = 0;
    textureMatrixST[1][1] = 1;
    textureMatrixST[1][2] = 0;
    textureMatrixST[1][3] = 0;

    // see if there is also a bump map specified
    if(const shaderStage_t *bumpStage = surf->material->GetBumpStage()) {
      RB_GetShaderTextureMatrix(surf->shaderRegisters, &bumpStage->texture, textureMatrixST);

      //void RB_GetShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture, idVec4 matrix[2] );
      bumpStage->texture.image->Bind(2);
    } else {
      globalImages->flatNormalMap->Bind(2);
    }

	fhRenderProgram::SetBumpMatrix(textureMatrixST[0], textureMatrixST[1]);
  }
  else {    

    //prefer depth blend settings from material. If material does not define a 
    // depth blend mode, look at the geometry for depth blend settings (usually
    // set by particle systems for soft particles)

    depthBlendMode_t depthBlendMode = pStage->depthBlendMode;
    float depthBlendRange = pStage->depthBlendRange;

    if(depthBlendMode == DBM_UNDEFINED) {
      depthBlendMode = surf->geo->depthBlendMode;
      depthBlendRange = surf->geo->depthBlendRange;
    }      
    
    if (depthBlendMode == DBM_AUTO) {
      if (pStage->drawStateBits & (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE))
        depthBlendMode = DBM_COLORALPHA_ZERO;
      else if (pStage->drawStateBits & (GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO))
        depthBlendMode = DBM_COLORALPHA_ONE;
      else
        depthBlendMode = DBM_OFF;
    }

    if(depthBlendMode != DBM_OFF && depthBlendRange > 0.0f) {
      programWasReset = GL_UseProgram(depthblendProgram);

      globalImages->currentDepthImage->Bind(2);

      fhRenderProgram::SetDepthBlendRange(depthBlendRange);
      fhRenderProgram::SetDepthBlendMode(static_cast<int>(depthBlendMode));      
	  fhRenderProgram::SetClipRange(backEnd.viewDef->viewFrustum.GetNearDistance(), backEnd.viewDef->viewFrustum.GetFarDistance());      
    }
    else {
      programWasReset = GL_UseProgram(defaultProgram);
    }

	GL_SetupVertexAttributes(fhVertexLayout::DrawPosColorTexOnly, offset);
  }

  // bind the texture
  if (pStage->texture.cinematic) {
	  if (r_skipDynamicTextures.GetBool()) {
		  globalImages->defaultImage->Bind(1);		  
	  }
	  else {
		  // offset time by shaderParm[7] (FIXME: make the time offset a parameter of the shader?)
		  // We make no attempt to optimize for multiple identical cinematics being in view, or
		  // for cinematics going at a lower framerate than the renderer.
		  cinData_t	cin = pStage->texture.cinematic->ImageForTime( (int)(1000 * (backEnd.viewDef->floatTime + backEnd.viewDef->renderView.shaderParms[11])) );

		  if (cin.image) {
			  globalImages->cinematicImage->UploadScratch( 1, cin.image, cin.imageWidth, cin.imageHeight );
		  }
		  else {
			  globalImages->blackImage->Bind(1);
		  }
	  }
  }
  else {
	  //FIXME: see why image is invalid
	  if (pStage->texture.image) {
		  pStage->texture.image->Bind(1);
	  }
  }
  
  // set the state
  GL_State(pStage->drawStateBits);  

	if(programWasReset) {
		// current render
		const int	w = backEnd.viewDef->viewport.x2 - backEnd.viewDef->viewport.x1 + 1;
		const int	h = backEnd.viewDef->viewport.y2 - backEnd.viewDef->viewport.y1 + 1;
		fhRenderProgram::SetCurrentRenderSize(
			idVec2( globalImages->currentRenderImage->uploadWidth, globalImages->currentRenderImage->uploadHeight ),
			idVec2( w, h ) );
	}
  
	if (surf->space->modelDepthHack != 0.0f) {
		RB_EnterModelDepthHack( surf->space->modelDepthHack );
	}
	else if (surf->space->weaponDepthHack) {
		RB_EnterWeaponDepthHack();
	}
	else if (programWasReset) {
		fhRenderProgram::SetProjectionMatrix(backEnd.viewDef->projectionMatrix);
	}

	if(surf->space != backEnd.currentSpace || programWasReset) {
		fhRenderProgram::SetModelMatrix( surf->space->modelMatrix );
		fhRenderProgram::SetModelViewMatrix( surf->space->modelViewMatrix );	
	}  

  switch (pStage->vertexColor) {
  case SVC_IGNORE:
	fhRenderProgram::SetColorModulate(idVec4::zero);
	fhRenderProgram::SetColorAdd(idVec4::one);
    break;
  case SVC_MODULATE:
	fhRenderProgram::SetColorModulate(idVec4::one);
	fhRenderProgram::SetColorAdd(idVec4::zero);
    break;
  case SVC_INVERSE_MODULATE:
	fhRenderProgram::SetColorModulate(idVec4::negOne);
	fhRenderProgram::SetColorAdd(idVec4::one);
    break;
  }     

  // set privatePolygonOffset if necessary
  if (pStage->privatePolygonOffset) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset);
  }

  if(pStage->texture.image) {
    // set the texture matrix
    idVec4 textureMatrix[2];
    textureMatrix[0][0] = 1;
    textureMatrix[0][1] = 0;
    textureMatrix[0][2] = 0;
    textureMatrix[0][3] = 0;
    textureMatrix[1][0] = 0;
    textureMatrix[1][1] = 1;
    textureMatrix[1][2] = 0;
    textureMatrix[1][3] = 0;
    if(pStage->texture.hasMatrix) {
      RB_GetShaderTextureMatrix(surf->shaderRegisters, &pStage->texture, textureMatrix);
	}

	fhRenderProgram::SetBumpMatrix(textureMatrix[0], textureMatrix[1]);  

    pStage->texture.image->Bind(1);    
  }

  fhRenderProgram::SetDiffuseColor(idVec4(color));

  // draw it
  RB_DrawElementsWithCounters(surf->geo);  

  if (surf->space->modelDepthHack != 0.0f || surf->space->weaponDepthHack) {
	  RB_LeaveDepthHack();
  }
}


/*
==================
RB_GLSL_DrawInteraction
==================
*/
void RB_GLSL_DrawInteraction(const drawInteraction_t *din) {  

	// change the matrix and light projection vectors if needed
	if (din->surf->space != backEnd.currentSpace) {		
		fhRenderProgram::SetModelViewMatrix( din->surf->space->modelViewMatrix );
		fhRenderProgram::SetModelMatrix(din->surf->space->modelMatrix);				

		if (din->surf->space->modelDepthHack != 0.0f) {
			RB_EnterModelDepthHack( din->surf->space->modelDepthHack );
		}
		else 	if (din->surf->space->weaponDepthHack) {
			RB_EnterWeaponDepthHack();
		}
		else if (!backEnd.currentSpace || backEnd.currentSpace->modelDepthHack || backEnd.currentSpace->weaponDepthHack) {
			RB_LeaveDepthHack();
		}

		backEnd.currentSpace = din->surf->space;
	}

	fhRenderProgram::SetLocalViewOrigin( din->localViewOrigin );
	fhRenderProgram::SetLocalLightOrigin( din->localLightOrigin );
	fhRenderProgram::SetLightProjectionMatrix( din->lightProjection[0], din->lightProjection[1], din->lightProjection[2] );
	fhRenderProgram::SetLightFallOff( din->lightProjection[3] );	
	fhRenderProgram::SetBumpMatrix(din->bumpMatrix[0], din->bumpMatrix[1]);
	fhRenderProgram::SetDiffuseMatrix(din->diffuseMatrix[0], din->diffuseMatrix[1]);
	fhRenderProgram::SetSpecularMatrix(din->specularMatrix[0], din->specularMatrix[1]);

	switch (din->vertexColor) {
	case SVC_IGNORE:
		fhRenderProgram::SetColorModulate( idVec4::zero );
		fhRenderProgram::SetColorAdd( idVec4::one );
		break;
	case SVC_MODULATE:
		fhRenderProgram::SetColorModulate( idVec4::one );
		fhRenderProgram::SetColorAdd( idVec4::zero );
		break;
	case SVC_INVERSE_MODULATE:
		fhRenderProgram::SetColorModulate( idVec4::negOne );
		fhRenderProgram::SetColorAdd( idVec4::one );
		break;
	}

	fhRenderProgram::SetDiffuseColor(din->diffuseColor);
	fhRenderProgram::SetSpecularColor(din->specularColor * r_specularScale.GetFloat());

	if( r_pomEnabled.GetBool() && din->specularImage->hasAlpha ) {
		fhRenderProgram::SetPomMaxHeight(r_pomMaxHeight.GetFloat());
	} else {
		fhRenderProgram::SetPomMaxHeight(-1);	  
	}

	// texture 1 will be the per-surface bump map  
	din->bumpImage->Bind(1);

	// texture 2 will be the light falloff texture  
	din->lightFalloffImage->Bind(2);

	// texture 3 will be the light projection texture  
	din->lightImage->Bind(3);

	// texture 4 is the per-surface diffuse map  
	din->diffuseImage->Bind(4);

	// texture 5 is the per-surface specular map  
	din->specularImage->Bind(5);

	// draw it
	RB_DrawElementsWithCounters(din->surf->geo);
}

/*
=============
RB_GLSL_CreateDrawInteractions

=============
*/
void RB_GLSL_CreateDrawInteractions(const drawSurf_t *surf) {
	assert(interactionProgram);

	if (!surf) {
		return;
	}

	// perform setup here that will be constant for all interactions
	GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc);
  
	GL_UseProgram( interactionProgram );

	fhRenderProgram::SetShading( r_shading.GetInteger() );
	fhRenderProgram::SetSpecularExp( r_specularExp.GetFloat() );

	if (backEnd.vLight->lightDef->ShadowMode() == shadowMode_t::ShadowMap) {
		const idVec4 globalLightOrigin = idVec4( backEnd.vLight->globalLightOrigin, 1 );
		fhRenderProgram::SetGlobalLightOrigin( globalLightOrigin );

		const float shadowBrightness = backEnd.vLight->lightDef->ShadowBrightness();
		const float shadowSoftness = backEnd.vLight->lightDef->ShadowSoftness();
		fhRenderProgram::SetShadowParams( idVec4( shadowSoftness, shadowBrightness, backEnd.vLight->nearClip[0], backEnd.vLight->farClip[0] ) );

		if (backEnd.vLight->lightDef->parms.pointLight) {
			//point light
			fhRenderProgram::SetShadowMappingMode( 1 );
			fhRenderProgram::SetPointLightProjectionMatrices( backEnd.vLight->viewProjectionMatrices[0].ToFloatPtr() );
		}
		else {
			//projected light
			fhRenderProgram::SetShadowMappingMode( 2 );
			fhRenderProgram::SetSpotLightProjectionMatrix( backEnd.vLight->viewProjectionMatrices[0].ToFloatPtr() );
		}
	}
	else {
		//no shadows
		fhRenderProgram::SetShadowMappingMode( 0 );
	}

	backEnd.currentSpace = nullptr;
	for (; surf; surf = surf->nextOnLight) {
		// perform setup here that will not change over multiple interaction passes

		// set the vertex pointers
		const auto offset = vertexCache.Bind(surf->geo->ambientCache);
		GL_SetupVertexAttributes(fhVertexLayout::Draw, offset);

		// this may cause RB_ARB2_DrawInteraction to be exacuted multiple
		// times with different colors and images if the surface or light have multiple layers
		RB_CreateSingleDrawInteractions(surf, RB_GLSL_DrawInteraction);
	}  

	//just make sure no depth hack is active anymore.
	if (backEnd.currentSpace && (backEnd.currentSpace->modelDepthHack || backEnd.currentSpace->weaponDepthHack)) {
		RB_LeaveDepthHack();		
	}
	backEnd.currentSpace = nullptr;	
}



/*
=================
RB_SubmittInteraction
=================
*/
static void RB_SubmittInteraction( drawInteraction_t *din, InteractionList& interactionList ) {
	if (!din->bumpImage) {
		return;
	}

	if (!din->diffuseImage || r_skipDiffuse.GetBool()) {
		din->diffuseImage = globalImages->blackImage;
	}
	if (!din->specularImage || r_skipSpecular.GetBool() || din->ambientLight) {
		din->specularImage = globalImages->blackImage;
	}
	if (!din->bumpImage || r_skipBump.GetBool()) {
		din->bumpImage = globalImages->flatNormalMap;
	}

	// if we wouldn't draw anything, don't call the Draw function
	if (
		((din->diffuseColor[0] > 0 ||
		din->diffuseColor[1] > 0 ||
		din->diffuseColor[2] > 0) && din->diffuseImage != globalImages->blackImage)
		|| ((din->specularColor[0] > 0 ||
		din->specularColor[1] > 0 ||
		din->specularColor[2] > 0) && din->specularImage != globalImages->blackImage)) {

		interactionList.Append(*din);
	}
}

static idList<viewLight_t*> shadowCastingViewLights[3];
static idList<viewLight_t*> batch;



/*
==================
RB_GLSL_DrawInteractions
==================
*/
void RB_GLSL_DrawInteractions( void ) {

	InteractionList interactionList;

	batch.SetNum( 0 );
	shadowCastingViewLights[0].SetNum( 0 );
	shadowCastingViewLights[1].SetNum( 0 );
	shadowCastingViewLights[2].SetNum( 0 );

	for (viewLight_t* vLight = backEnd.viewDef->viewLights; vLight; vLight = vLight->next) {
		// do fogging later
		if (vLight->lightShader->IsFogLight()) {
			continue;
		}

		if (vLight->lightShader->IsBlendLight()) {
			continue;
		}

		if (!vLight->localInteractions && !vLight->globalInteractions && !vLight->translucentInteractions) {
			continue;
		}

		if (vLight->lightDef->ShadowMode() == shadowMode_t::ShadowMap) {
			int lod = Min( 2, Max( vLight->shadowMapLod, 0 ) );
			shadowCastingViewLights[lod].Append( vLight );
		}
		else {
			//light does not require shadow maps to be rendered. Render this light with the first batch.
			batch.Append( vLight );
		}
	}

	while(true) {
		if (shadowCastingViewLights[0].Num() != 0 || shadowCastingViewLights[1].Num() != 0 || shadowCastingViewLights[2].Num() != 0) {
			GL_UseProgram( shadowmapProgram );
			glStencilFunc( GL_ALWAYS, 0, 255 );
			GL_State( GLS_DEPTHFUNC_LESS | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );	// make sure depth mask is off before clear
			glDepthMask( GL_TRUE );
			glEnable( GL_DEPTH_TEST );

			globalImages->BindNull( 6 );
			globalImages->shadowmapFramebuffer->Bind();
			glViewport( 0, 0, globalImages->shadowmapFramebuffer->GetWidth(), globalImages->shadowmapFramebuffer->GetHeight() );
			glScissor( 0, 0, globalImages->shadowmapFramebuffer->GetWidth(), globalImages->shadowmapFramebuffer->GetHeight() );
			glClear( GL_DEPTH_BUFFER_BIT );

			for(int lod = 0; lod < 3; ++lod) {
				idList<viewLight_t*>& lights = shadowCastingViewLights[lod];

				while (lights.Num() > 0) {
					backEnd.vLight = lights.Last();

					if (RB_RenderShadowMaps(backEnd.vLight)) {	
						//shadow map was rendered successfully, so add the light to the next batch
						batch.Append( backEnd.vLight );			
						lights.RemoveLast();
					}
					else {
						break;
					}
				}
			}

			globalImages->defaultFramebuffer->Bind();
			globalImages->shadowmapImage->Bind( 6 );
			globalImages->jitterImage->Bind( 7 );
		}

		glDisable( GL_POLYGON_OFFSET_FILL );
		glEnable( GL_CULL_FACE );
		glFrontFace( GL_CCW );

		//reset viewport 
		glViewport( tr.viewportOffset[0] + backEnd.viewDef->viewport.x1,
			tr.viewportOffset[1] + backEnd.viewDef->viewport.y1,
			backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
			backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1 );

		// the scissor may be smaller than the viewport for subviews
		glScissor( tr.viewportOffset[0] + backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
			tr.viewportOffset[1] + backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
			backEnd.viewDef->scissor.x2 + 1 - backEnd.viewDef->scissor.x1,
			backEnd.viewDef->scissor.y2 + 1 - backEnd.viewDef->scissor.y1 );
		backEnd.currentScissor = backEnd.viewDef->scissor;

		for(int i=0; i<batch.Num(); ++i) {
			viewLight_t* vLight = batch[i];
			backEnd.vLight = vLight;
		
			backEnd.stats.groups[backEndGroup::Interaction].passes += 1;
			fhTimeElapsed timeElapsed( &backEnd.stats.groups[backEndGroup::Interaction].time );
			
			const idMaterial* lightShader = vLight->lightShader;

			// clear the stencil buffer if needed
			if (vLight->globalShadows || vLight->localShadows) {
				backEnd.currentScissor = vLight->scissorRect;
				if (r_useScissor.GetBool()) {
					glScissor( backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
						backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
						backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
						backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1 );
				}
				glClear( GL_STENCIL_BUFFER_BIT );
			}
			else {
				// no shadows, so no need to read or write the stencil buffer
				// we might in theory want to use GL_ALWAYS instead of disabling
				// completely, to satisfy the invarience rules
				glStencilFunc( GL_ALWAYS, 128, 255 );
			}

			RB_GLSL_StencilShadowPass( vLight->globalShadows );
			interactionList.Clear();
			interactionList.AddDrawSurfacesOnLight( vLight->localInteractions );
			interactionList.Submit();

			RB_GLSL_StencilShadowPass( vLight->localShadows );
			interactionList.Clear();
			interactionList.AddDrawSurfacesOnLight( vLight->globalInteractions );
			interactionList.Submit();

			// translucent surfaces never get stencil shadowed
			if (r_skipTranslucent.GetBool()) {
				continue;
			}

			glStencilFunc( GL_ALWAYS, 128, 255 );

			backEnd.depthFunc = GLS_DEPTHFUNC_LESS;

			interactionList.Clear();
			interactionList.AddDrawSurfacesOnLight( vLight->translucentInteractions );
			interactionList.Submit();

			backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
		}

		RB_FreeAllShadowMaps();
		batch.SetNum(0);

		if (shadowCastingViewLights[0].Num() == 0 && shadowCastingViewLights[1].Num() == 0 && shadowCastingViewLights[2].Num() == 0) {
			break;
		}
	}

	glStencilFunc( GL_ALWAYS, 128, 255 );
}