#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_local.h"

#define MAX_GLPROGS 128
static glslProgramDef_t glslPrograms[MAX_GLPROGS] = { 0 };

static const char* const shadowVertexShaderName = "shadow.vp";
static const char* const shadowFragmentShaderName = "shadow.fp";

static const glslProgramDef_t* shadowProgram = nullptr;
static const glslProgramDef_t* interactionProgram = nullptr;
static const glslProgramDef_t* depthProgram = nullptr;
static const glslProgramDef_t* defaultProgram = nullptr;
//static const glslProgramDef_t* diffuseCubeProgram = nullptr;
static const glslProgramDef_t* skyboxProgram = nullptr;
static const glslProgramDef_t* bumpyEnvProgram = nullptr;

/*
====================
GL_SelectTextureNoClient
====================
*/
static void GL_SelectTextureNoClient(int unit) {
  backEnd.glState.currenttmu = unit;
  glActiveTextureARB(GL_TEXTURE0_ARB + unit);
  RB_LogComment("glActiveTextureARB( %i )\n", unit);
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
    glUniform4fv(glslProgramDef_t::uniform_localLightOrigin, 1, localLight.ToFloatPtr());
    glUniformMatrix4fv(glslProgramDef_t::uniform_projectionMatrix, 1, false, GL_ProjectionMatrix.Top());
    glUniformMatrix4fv(glslProgramDef_t::uniform_modelViewMatrix, 1, false, GL_ModelViewMatrix.Top());
  }

  tri = surf->geo;

  if (!tri->shadowCache) {
    return;
  }

  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_position_shadow);
  glVertexAttribPointer(glslProgramDef_t::vertex_attrib_position_shadow, 4, GL_FLOAT, false, sizeof(shadowCache_t), vertexCache.Position(tri->shadowCache));

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
    glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_position_shadow);

    return;
  }

  // patent-free work around
  if (!external) {
    // "preload" the stencil buffer with the number of volumes
    // that get clipped by the near or far clip plane
    glStencilOp(GL_KEEP, tr.stencilDecr, tr.stencilDecr);
    GL_Cull(CT_FRONT_SIDED);
    RB_DrawShadowElementsWithCounters(tri, numIndexes);
    glStencilOp(GL_KEEP, tr.stencilIncr, tr.stencilIncr);
    GL_Cull(CT_BACK_SIDED);
    RB_DrawShadowElementsWithCounters(tri, numIndexes);
  }

  // traditional depth-pass stencil shadows
  glStencilOp(GL_KEEP, GL_KEEP, tr.stencilIncr);
  GL_Cull(CT_FRONT_SIDED);
  RB_DrawShadowElementsWithCounters(tri, numIndexes);

  glStencilOp(GL_KEEP, GL_KEEP, tr.stencilDecr);
  GL_Cull(CT_BACK_SIDED);
  RB_DrawShadowElementsWithCounters(tri, numIndexes);

  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_position_shadow);
}


/*
=====================
RB_GLSL_StencilShadowPass

Stencil test should already be enabled, and the stencil buffer should have
been set to 128 on any surfaces that might receive shadows
=====================
*/



void RB_GLSL_StencilShadowPass(const drawSurf_t *drawSurfs) {
  if (!r_shadows.GetBool()) {
    return;
  }

  if (!drawSurfs) {
    return;
  }

  if (!shadowProgram) {
    shadowProgram = R_FindGlslProgram(shadowVertexShaderName, shadowFragmentShaderName);

    if (!shadowProgram || !shadowProgram->ident) {
      return;
    }
  }

  glDisable(GL_VERTEX_PROGRAM_ARB);
  glDisable(GL_FRAGMENT_PROGRAM_ARB);
  glUseProgram(shadowProgram->ident);

  RB_LogComment("---------- RB_StencilShadowPass ----------\n");

  globalImages->BindNull();
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);

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

  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  glStencilFunc(GL_GEQUAL, 128, 255);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

  glUseProgram(0);
  glEnable(GL_VERTEX_PROGRAM_ARB);
}



/*
==================
RB_GLSL_FillDepthBuffer
==================
*/
void RB_GLSL_FillDepthBuffer(const drawSurf_t *surf) {
  int			stage;
  const idMaterial	*shader;
  const shaderStage_t *pStage;
  const float	*regs;
  float		color[4];
  const srfTriangles_t	*tri;

  tri = surf->geo;
  shader = surf->material;

  // update the clip plane if needed
  if (backEnd.viewDef->numClipPlanes && surf->space != backEnd.currentSpace) {
    GL_SelectTexture(1);

    idPlane	plane;

    R_GlobalPlaneToLocal(surf->space->modelMatrix, backEnd.viewDef->clipPlanes[0], plane);
    plane[3] += 0.5;	// the notch is in the middle
    glTexGenfv(GL_S, GL_OBJECT_PLANE, plane.ToFloatPtr());
    GL_SelectTexture(0);
  }

  if (!shader->IsDrawn()) {
    return;
  }

  // some deforms may disable themselves by setting numIndexes = 0
  if (!tri->numIndexes) {
    return;
  }

  // translucent surfaces don't put anything in the depth buffer and don't
  // test against it, which makes them fail the mirror clip plane operation
  if (shader->Coverage() == MC_TRANSLUCENT) {
    return;
  }

  if (!tri->ambientCache) {
    common->Printf("RB_T_FillDepthBuffer: !tri->ambientCache\n");
    return;
  }

  // get the expressions for conditionals / color / texcoords
  regs = surf->shaderRegisters;

  // if all stages of a material have been conditioned off, don't do anything
  for (stage = 0; stage < shader->GetNumStages(); stage++) {
    pStage = shader->GetStage(stage);
    // check the stage enable condition
    if (regs[pStage->conditionRegister] != 0) {
      break;
    }
  }
  if (stage == shader->GetNumStages()) {
    return;
  }

  // set polygon offset if necessary
  if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * shader->GetPolygonOffset());
  }

  // subviews will just down-modulate the color buffer by overbright
  if (shader->GetSort() == SS_SUBVIEW) {
    GL_State(GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS);
    color[0] =
      color[1] =
      color[2] = (1.0 / backEnd.overBright);
    color[3] = 1;
  }
  else {
    // others just draw black
    color[0] = 0;
    color[1] = 0;
    color[2] = 0;
    color[3] = 1;
  }

  idDrawVert *ac = (idDrawVert *)vertexCache.Position(tri->ambientCache);
  glVertexAttribPointer(glslProgramDef_t::vertex_attrib_position, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->xyz.ToFloatPtr());
  glVertexAttribPointer(glslProgramDef_t::vertex_attrib_texcoord, 2, GL_FLOAT, false, sizeof(idDrawVert), reinterpret_cast<void *>(&ac->st));
  glUniformMatrix4fv(glslProgramDef_t::uniform_modelViewMatrix, 1, false, GL_ModelViewMatrix.Top());
  glUniformMatrix4fv(glslProgramDef_t::uniform_projectionMatrix, 1, false, GL_ProjectionMatrix.Top());

  bool drawSolid = false;

  if (shader->Coverage() == MC_OPAQUE) {
    drawSolid = true;
  }

  // we may have multiple alpha tested stages
  if (shader->Coverage() == MC_PERFORATED) {
    // if the only alpha tested stages are condition register omitted,
    // draw a normal opaque surface
    bool	didDraw = false;

    // perforated surfaces may have multiple alpha tested stages
    for (stage = 0; stage < shader->GetNumStages(); stage++) {
      pStage = shader->GetStage(stage);

      if (!pStage->hasAlphaTest) {
        continue;
      }

      // check the stage enable condition
      if (regs[pStage->conditionRegister] == 0) {
        continue;
      }

      // if we at least tried to draw an alpha tested stage,
      // we won't draw the opaque surface
      didDraw = true;

      // set the alpha modulate
      color[3] = regs[pStage->color.registers[3]];

      // skip the entire stage if alpha would be black
      if (color[3] <= 0) {
        continue;
      }

      // bind the texture
      pStage->texture.image->Bind();

      //RB_PrepareStageTexturing(pStage, surf, ac);
      {
        glUniform1i(glslProgramDef_t::uniform_alphaTestEnabled, 1);
        glUniform4fv(glslProgramDef_t::uniform_diffuse_color, 1, color);

        // set texture matrix and texGens      

        if (pStage->privatePolygonOffset && !surf->material->TestMaterialFlag(MF_POLYGONOFFSET)) {
          glEnable(GL_POLYGON_OFFSET_FILL);
          glPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * pStage->privatePolygonOffset);
        }

        if (pStage->texture.hasMatrix) {
          idVec4 textureMatrix[2];
          RB_GetShaderTextureMatrix(surf->shaderRegisters, &pStage->texture, textureMatrix);
          glUniform4fv(glslProgramDef_t::uniform_diffuseMatrixS, 1, textureMatrix[0].ToFloatPtr());
          glUniform4fv(glslProgramDef_t::uniform_diffuseMatrixT, 1, textureMatrix[1].ToFloatPtr());
        }
      }

      // draw it
      RB_DrawElementsWithCounters(tri);

      //RB_FinishStageTexturing(pStage, surf, ac);
      {
        if (pStage->privatePolygonOffset && !surf->material->TestMaterialFlag(MF_POLYGONOFFSET)) {
          glDisable(GL_POLYGON_OFFSET_FILL);
        }

        idVec4 textureMatrix[2];
        textureMatrix[0][0] = 1;
        textureMatrix[0][1] = 0;
        textureMatrix[0][2] = 0;
        textureMatrix[0][3] = 0;
        textureMatrix[1][0] = 0;
        textureMatrix[1][1] = 1;
        textureMatrix[1][2] = 0;
        textureMatrix[1][3] = 0;
        glUniform4fv(glslProgramDef_t::uniform_diffuseMatrixS, 1, textureMatrix[0].ToFloatPtr());
        glUniform4fv(glslProgramDef_t::uniform_diffuseMatrixT, 1, textureMatrix[1].ToFloatPtr());

        glUniform1i(glslProgramDef_t::uniform_alphaTestEnabled, 0);
      }
    }
    //glDisable(GL_ALPHA_TEST);
    if (!didDraw) {
      drawSolid = true;
    }
  }

  // draw the entire surface solid
  if (drawSolid) {
    glUniform4fv(glslProgramDef_t::uniform_diffuse_color, 1, color);
    //    glColor4fv(color);
    globalImages->whiteImage->Bind();

    // draw it
    RB_DrawElementsWithCounters(tri);
  }


  // reset polygon offset
  if (shader->TestMaterialFlag(MF_POLYGONOFFSET)) {
    glDisable(GL_POLYGON_OFFSET_FILL);
  }

  // reset blending
  if (shader->GetSort() == SS_SUBVIEW) {
    GL_State(GLS_DEPTHFUNC_LESS);
  }

}


/*
=====================
RB_GLSL_FillDepthBuffer

If we are rendering a subview with a near clip plane, use a second texture
to force the alpha test to fail when behind that clip plane
=====================
*/
void RB_GLSL_FillDepthBuffer(drawSurf_t **drawSurfs, int numDrawSurfs) {
  // if we are just doing 2D rendering, no need to fill the depth buffer
  if (!backEnd.viewDef->viewEntitys) {
    return;
  }

  RB_LogComment("---------- RB_GLSL_FillDepthBuffer ----------\n");

  // enable the second texture for mirror plane clipping if needed
  if (backEnd.viewDef->numClipPlanes) {
    GL_SelectTexture(1);
    globalImages->alphaNotchImage->Bind();
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_TEXTURE_GEN_S);
    glTexCoord2f(1, 0.5);
  }

  // the first texture will be used for alpha tested surfaces
  GL_SelectTexture(0);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  // decal surfaces may enable polygon offset
  glPolygonOffset(r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat());

  GL_State(GLS_DEPTHFUNC_LESS);

  // Enable stencil test if we are going to be using it for shadows.
  // If we didn't do this, it would be legal behavior to get z fighting
  // from the ambient pass and the light passes.
  glEnable(GL_STENCIL_TEST);
  glStencilFunc(GL_ALWAYS, 1, 255);

  if (!depthProgram) {
    depthProgram = R_FindGlslProgram("depth.vp", "depth.fp");
    if (!depthProgram)
      return;
  }

  glUseProgram(depthProgram->ident);
  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_position);
  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_texcoord);

  RB_RenderDrawSurfListWithFunction(drawSurfs, numDrawSurfs, RB_GLSL_FillDepthBuffer);

  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_position);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_texcoord);
  glUseProgram(depthProgram->ident);

  if (backEnd.viewDef->numClipPlanes) {
    GL_SelectTexture(1);
    globalImages->BindNull();
    glDisable(GL_TEXTURE_GEN_S);
    GL_SelectTexture(0);
  }

}



/*
=====================
RB_GLSL_RenderSpecialShaderStage
=====================
*/
void RB_GLSL_RenderSpecialShaderStage(const float* regs, const shaderStage_t* pStage, glslShaderStage_t* glslStage, const srfTriangles_t	*tri) {

  idDrawVert *ac = (idDrawVert *)vertexCache.Position(tri->ambientCache);

  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_position);
  glVertexAttribPointer(glslProgramDef_t::vertex_attrib_position, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->xyz.ToFloatPtr());

  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_texcoord);
  glVertexAttribPointer(glslProgramDef_t::vertex_attrib_texcoord, 2, GL_FLOAT, false, sizeof(idDrawVert), ac->st.ToFloatPtr());

  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_normal);
  glVertexAttribPointer(glslProgramDef_t::vertex_attrib_normal, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->normal.ToFloatPtr());

  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_color);
  glVertexAttribPointer(glslProgramDef_t::vertex_attrib_color, 4, GL_UNSIGNED_BYTE, false, sizeof(idDrawVert), (void *)&ac->color);

  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_binormal);
  glVertexAttribPointer(glslProgramDef_t::vertex_attrib_binormal, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->tangents[1].ToFloatPtr());

  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_tangent);
  glVertexAttribPointer(glslProgramDef_t::vertex_attrib_tangent, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->tangents[0].ToFloatPtr());

  GL_State(pStage->drawStateBits);
  glUseProgram(glslStage->program->ident);


  for (int i = 0; i < glslStage->numShaderParms; i++) {
    float	parm[4];
    parm[0] = regs[glslStage->shaderParms[i][0]];
    parm[1] = regs[glslStage->shaderParms[i][1]];
    parm[2] = regs[glslStage->shaderParms[i][2]];
    parm[3] = regs[glslStage->shaderParms[i][3]];
    glUniform4fv(glslProgramDef_t::uniform_shaderparm0 + i, 1, parm);
  }

  glUniformMatrix4fv(glslProgramDef_t::uniform_modelViewMatrix, 1, GL_FALSE, GL_ModelViewMatrix.Top());
  glUniformMatrix4fv(glslProgramDef_t::uniform_projectionMatrix, 1, GL_FALSE, GL_ProjectionMatrix.Top());

  for (int i = 0; i < glslStage->numShaderMaps; i++) {
    if (glslStage->shaderMap[i]) {
      GL_SelectTexture(i);
      glslStage->shaderMap[i]->Bind();
    }
  }

  // draw it
  RB_DrawElementsWithCounters(tri);

  for (int i = 0; i < glslStage->numShaderMaps; i++) {
    if (glslStage->shaderMap[i]) {
      GL_SelectTexture(i);
      globalImages->BindNull();
    }
  }

  glUseProgram(0);
  GL_SelectTexture(0);

  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_position);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_texcoord);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_normal);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_color);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_binormal);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_tangent);
}


/*
=================
R_PreprocessShader
=================
*/
static bool R_PreprocessShader(char* src, int srcsize, char* dest, int destsize) {
  static const char* const inc_stmt = "#include ";

  char* inc_start = strstr(src, inc_stmt);
  if(!inc_start) {
    if(srcsize >= destsize) {
      common->Warning(": File too large\n");
      return false;
    }     

    memcpy(dest, src, srcsize);
    dest[srcsize] = '\0';

    return true;
  }

  char* filename_start = strstr(inc_start, "\"");
  if(!filename_start)
    return false;
  
  filename_start++;

  char* filename_stop = strstr(filename_start, "\"");
  if(!filename_stop)
    return false;  

  int filename_len = (ptrdiff_t)filename_stop - (ptrdiff_t)filename_start;
  filename_stop++;

  int bytes_before_inc = (ptrdiff_t)inc_start - (ptrdiff_t)src;
  int bytes_after_inc = (ptrdiff_t)srcsize - ((ptrdiff_t)filename_stop - (ptrdiff_t)src);
  
  idStr fullPath = idStr("glsl/") + idStr(filename_start, 0, filename_len);

  char	*fileBuffer = nullptr;
  fileSystem->ReadFile(fullPath.c_str(), (void **)&fileBuffer, NULL);
  if (!fileBuffer) {
    common->Printf(": File not found\n");
    return false;
  }
  int file_size = strlen(fileBuffer);

  if(file_size + bytes_before_inc + bytes_after_inc >= destsize) {
    common->Printf(": File too large\n");
    fileSystem->FreeFile(fileBuffer);
    return false;
  }

  memcpy(dest, src, bytes_before_inc);
  dest += bytes_before_inc;
  memcpy(dest, fileBuffer, file_size);
  dest += file_size;
  memcpy(dest, filename_stop, bytes_after_inc);
  dest[bytes_after_inc] = '\0';
  return true;
}


/*
=================
R_LoadGlslShader
=================
*/
static GLuint R_LoadGlslShader(GLenum shaderType, const char* filename) {
  assert(filename);
  assert(filename[0]);
  assert(shaderType == GL_VERTEX_SHADER || shaderType == GL_FRAGMENT_SHADER);

  idStr	fullPath = "glsl/";
  fullPath += filename;

  if(shaderType == GL_VERTEX_SHADER)
    common->Printf("load vertex shader %s\n", fullPath.c_str());
  else
    common->Printf("load fragment shader %s\n", fullPath.c_str());

  // load the program even if we don't support it, so
  // fs_copyfiles can generate cross-platform data dumps
  // 
  char	*fileBuffer = nullptr;
  fileSystem->ReadFile(fullPath.c_str(), (void **)&fileBuffer, NULL);
  if (!fileBuffer) {
    common->Printf(": File not found\n");
    return 0;
  }

  const int buffer_size = 1024 * 256;
  char* buffer = new char[buffer_size];

  bool ok = R_PreprocessShader(fileBuffer, strlen(fileBuffer), buffer, buffer_size);

  fileSystem->FreeFile(fileBuffer);

  if(!ok) {
    return 0;
  }

  GLuint shaderObject = glCreateShader(shaderType);

  const int bufferLen = strlen(buffer);

  glShaderSource(shaderObject, 1, (const GLchar**)&buffer, &bufferLen);
  glCompileShader(shaderObject);

  delete buffer;

  GLint success = 0;
  glGetShaderiv(shaderObject, GL_COMPILE_STATUS, &success);
  if(success = GL_FALSE) {
    char buffer[1024];
    GLsizei length;
    glGetShaderInfoLog(shaderObject, sizeof(buffer)-1, &length, &buffer[0]);
    buffer[length] = '\0';


    common->Printf("failed to compile shader '%s': %s", filename, &buffer[0]);
    return 0;
  }

  return shaderObject;
}

namespace {
  // Little RAII helper to deal with detaching and deleting of shader objects
  struct GlslShader {
    GlslShader(GLuint ident)
      : ident(ident)
      , program(0)
    {}

    ~GlslShader() {      
      release();        
    }

    void release() {
      if (ident) {
        if (program) {
          glDetachShader(program, ident);
        }
        glDeleteShader(ident);
      }

      ident = 0;
      program = 0;
    }

    void attachToProgram(GLuint program) {
      this->program = program;
      glAttachShader(program, ident);
    }

    GLuint ident;
    GLuint program;
  };
}

/*
=================
R_LoadGlslProgram
=================
*/
static void R_LoadGlslProgram(glslProgramDef_t& programDef) {
  if(!programDef.vertexShaderName[0] || !programDef.fragmentShaderName[0])
    return;

  GlslShader vertexShader = R_LoadGlslShader(GL_VERTEX_SHADER, programDef.vertexShaderName);
  if (!vertexShader.ident) {
    common->Warning("failed to load GLSL vertex shader: %s", programDef.vertexShaderName);
    return;
  }

  GlslShader fragmentShader = R_LoadGlslShader(GL_FRAGMENT_SHADER, programDef.fragmentShaderName);
  if (!fragmentShader.ident) {
    common->Warning("failed to load GLSL fragment shader: %s", programDef.fragmentShaderName);
    return;
  }

  const GLuint program = glCreateProgram();
  if (!program) {
    common->Warning("failed to create GLSL program object");
    return;
  }

  vertexShader.attachToProgram(program);
  fragmentShader.attachToProgram(program);
  glLinkProgram(program);

  GLint isLinked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
  if (isLinked == GL_FALSE) {

    char buffer[1024];
    GLsizei length;
    glGetProgramInfoLog(program, sizeof(buffer)-1, &length, &buffer[0]);
    buffer[length] = '\0';

    vertexShader.release();
    fragmentShader.release();
    glDeleteProgram(program);

    common->Warning("failed to link GLSL shaders to program: %s", &buffer[0]);
    return;
  }

  if (programDef.ident)
    glDeleteProgram(programDef.ident);

  programDef.ident = program;
}

/*
=================
R_FindGlslProgram
=================
*/
const glslProgramDef_t* R_FindGlslProgram(const char* vertexShaderName, const char* fragmentShaderName) {
  assert(vertexShaderName && vertexShaderName[0]);
  assert(fragmentShaderName && fragmentShaderName[0]);

  const int vertexShaderNameLen = strlen(vertexShaderName);
  const int fragmentShaderNameLen = strlen(fragmentShaderName);

  int i;
  for (i = 0; i < MAX_GLPROGS; ++i) {
    const int vsLen = strlen(glslPrograms[i].vertexShaderName);
    const int fsLen = strlen(glslPrograms[i].fragmentShaderName);
    
    if(!vsLen || !fsLen)
      break;

    if(vsLen != vertexShaderNameLen || fsLen != fragmentShaderNameLen)
      continue;

    if(idStr::Icmpn(vertexShaderName, glslPrograms[i].vertexShaderName, vsLen) != 0)
      continue;

    if (idStr::Icmpn(fragmentShaderName, glslPrograms[i].fragmentShaderName, fsLen) != 0)
      continue;

    return &glslPrograms[i];
  }

  if(i >= MAX_GLPROGS) {
    common->Error("cannot create GLSL program, maximum number of programs reached");
    return nullptr;
  }

  strncpy(glslPrograms[i].vertexShaderName, vertexShaderName, vertexShaderNameLen);
  strncpy(glslPrograms[i].fragmentShaderName, fragmentShaderName, fragmentShaderNameLen);

  R_LoadGlslProgram(glslPrograms[i]);

  return &glslPrograms[i];
}


/*
=================
R_ReloadGlslPrograms_f
=================
*/
void R_ReloadGlslPrograms_f( const idCmdArgs &args ) {
  common->Printf( "----- R_ReloadGlslPrograms -----\n" );

  for(int i=0; i<MAX_GLPROGS; ++i) {
    R_LoadGlslProgram(glslPrograms[i]);
  }

  common->Printf( "----- R_ReloadGlslPrograms -----\n" );
}


/*
====================
RB_GLSL_RenderShaderStage
====================
*/
void RB_GLSL_RenderShaderStage(const drawSurf_t *surf, const shaderStage_t* pStage) {

  if(!defaultProgram) {
    defaultProgram = R_FindGlslProgram("default.vp", "default.fp");
    if(!defaultProgram)
      return;
  }
/*
  if (!diffuseCubeProgram) {
    diffuseCubeProgram = R_FindGlslProgram("diffuseCube.vp", "diffuseCube.fp");
    if (!diffuseCubeProgram)
      return;
  }
*/
  if (!skyboxProgram) {
    skyboxProgram = R_FindGlslProgram("skybox.vp", "skybox.fp");
    if (!skyboxProgram)
      return;
  }

  if (!bumpyEnvProgram) {
    bumpyEnvProgram = R_FindGlslProgram("bumpyenv.vp", "bumpyenv.fp");
    if (!bumpyEnvProgram)
      return;
  }

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
  
  idDrawVert *ac = (idDrawVert *)vertexCache.Position(surf->geo->ambientCache); 


  if (pStage->texture.texgen == TG_DIFFUSE_CUBE) {
    return;
  }
  else if (pStage->texture.texgen == TG_SKYBOX_CUBE || pStage->texture.texgen == TG_WOBBLESKY_CUBE) {
    glUseProgram(skyboxProgram->ident);

    idMat4 textureMatrix = mat4_identity;
    if (pStage->texture.texgen == TG_WOBBLESKY_CUBE) {
      R_CreateWobbleskyTexMatrix(surf, backEnd.viewDef->floatTime, textureMatrix.ToFloatPtr());
    }

    idVec4 localViewOrigin;
    R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3() );
    localViewOrigin[3] = 1.0f;
    glUniform4fv(glslProgramDef_t::uniform_localViewOrigin, 1, localViewOrigin.ToFloatPtr());
    glUniformMatrix4fv(glslProgramDef_t::uniform_textureMatrix0, 1, false, textureMatrix.ToFloatPtr());

    glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_position);    
    glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_color);

    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_position, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->xyz.ToFloatPtr());
    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_color, 4, GL_UNSIGNED_BYTE, false, sizeof(idDrawVert), (void *)&ac->color);    

    glUniform1i(glslProgramDef_t::uniform_cubemap1, 1);
  }
  else if (pStage->texture.texgen == TG_SCREEN) {
    return;
  }
  else if (pStage->texture.texgen == TG_GLASSWARP) {
    return;
  }
  else if (pStage->texture.texgen == TG_REFLECT_CUBE) {

    glUseProgram(bumpyEnvProgram->ident);

    idMat4 textureMatrix = mat4_identity;

    idVec4 localViewOrigin;
    R_GlobalPointToLocal(surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewOrigin.ToVec3());
    localViewOrigin[3] = 1.0f;
    glUniform4fv(glslProgramDef_t::uniform_localViewOrigin, 1, localViewOrigin.ToFloatPtr());
    glUniformMatrix4fv(glslProgramDef_t::uniform_textureMatrix0, 1, false, textureMatrix.ToFloatPtr());

    glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_position);
    glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_texcoord);
    glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_normal);
    glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_color);
    glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_binormal);
    glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_tangent);

    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_position, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->xyz.ToFloatPtr());
    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_texcoord, 2, GL_FLOAT, false, sizeof(idDrawVert), ac->st.ToFloatPtr());
    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_normal, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->normal.ToFloatPtr());
    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_color, 4, GL_UNSIGNED_BYTE, false, sizeof(idDrawVert), (void *)&ac->color);
    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_binormal, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->tangents[1].ToFloatPtr());
    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_tangent, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->tangents[0].ToFloatPtr());

    glUniform1i(glslProgramDef_t::uniform_cubemap1, 1);
    glUniform1i(glslProgramDef_t::uniform_texture2, 2);

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
    GL_SelectTextureNoClient(2);
    if(const shaderStage_t *bumpStage = surf->material->GetBumpStage()) {
      RB_GetShaderTextureMatrix(surf->shaderRegisters, &bumpStage->texture, textureMatrixST);

      //void RB_GetShaderTextureMatrix( const float *shaderRegisters, const textureStage_t *texture, idVec4 matrix[2] );
      bumpStage->texture.image->Bind();
    } else {
      globalImages->flatNormalMap->Bind();
    }
    GL_SelectTextureNoClient(0);

    glUniform4fv(glslProgramDef_t::uniform_bumpMatrixS, 1, textureMatrixST[0].ToFloatPtr());
    glUniform4fv(glslProgramDef_t::uniform_bumpMatrixT, 1, textureMatrixST[1].ToFloatPtr());

  }
  else {
    glUseProgram(defaultProgram->ident);

    glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_position);
    glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_texcoord);
    glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_color);

    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_position, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->xyz.ToFloatPtr());
    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_texcoord, 2, GL_FLOAT, false, sizeof(idDrawVert), ac->st.ToFloatPtr());
    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_color, 4, GL_UNSIGNED_BYTE, false, sizeof(idDrawVert), (void *)&ac->color);

    glUniform1i(glslProgramDef_t::uniform_texture1, 1);
  }

  // bind the texture
  RB_BindVariableStageImage(&pStage->texture, surf->shaderRegisters);

  // set the state
  GL_State(pStage->drawStateBits);

  static const float zero[4] = { 0, 0, 0, 0 };
  static const float one[4] = { 1, 1, 1, 1 };
  static const float negOne[4] = { -1, -1, -1, -1 };

  switch (pStage->vertexColor) {
  case SVC_IGNORE:
    glUniform4fv(glslProgramDef_t::uniform_color_modulate, 1, zero);
    glUniform4fv(glslProgramDef_t::uniform_color_add, 1, one);
    break;
  case SVC_MODULATE:
    glUniform4fv(glslProgramDef_t::uniform_color_modulate, 1, one);
    glUniform4fv(glslProgramDef_t::uniform_color_add, 1, zero);
    break;
  case SVC_INVERSE_MODULATE:
    glUniform4fv(glslProgramDef_t::uniform_color_modulate, 1, negOne);
    glUniform4fv(glslProgramDef_t::uniform_color_add, 1, one);
    break;
  }  

  glUniformMatrix4fv(glslProgramDef_t::uniform_modelMatrix, 1, false, surf->space->modelMatrix);
  glUniformMatrix4fv(glslProgramDef_t::uniform_modelViewMatrix, 1, false, GL_ModelViewMatrix.Top());
  glUniformMatrix4fv(glslProgramDef_t::uniform_projectionMatrix, 1, false, GL_ProjectionMatrix.Top());

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
    if(pStage->texture.hasMatrix)
      RB_GetShaderTextureMatrix(surf->shaderRegisters, &pStage->texture, textureMatrix);
    glUniform4fv(glslProgramDef_t::uniform_bumpMatrixS, 1, textureMatrix[0].ToFloatPtr());
    glUniform4fv(glslProgramDef_t::uniform_bumpMatrixT, 1, textureMatrix[1].ToFloatPtr());    
  
    GL_SelectTextureNoClient(1);
    pStage->texture.image->Bind();
    GL_SelectTextureNoClient(0);
  }

  glUniform4fv(glslProgramDef_t::uniform_diffuse_color, 1, color);

  // draw it
  RB_DrawElementsWithCounters(surf->geo);

  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_position);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_texcoord);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_normal);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_color);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_binormal);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_tangent);
  glUseProgram(0);
}


/*
==================
RB_GLSL_DrawInteraction
==================
*/
void	RB_GLSL_DrawInteraction(const drawInteraction_t *din) {  

  glUniformMatrix4fv(glslProgramDef_t::uniform_modelViewMatrix, 1, false, GL_ModelViewMatrix.Top());
  glUniformMatrix4fv(glslProgramDef_t::uniform_projectionMatrix, 1, false, GL_ProjectionMatrix.Top());

  glUniform4fv(glslProgramDef_t::uniform_localLightOrigin, 1, din->localLightOrigin.ToFloatPtr());
  glUniform4fv(glslProgramDef_t::uniform_localViewOrigin, 1, din->localViewOrigin.ToFloatPtr());
  
  glUniform4fv(glslProgramDef_t::uniform_lightProjectionMatrixS, 1, din->lightProjection[0].ToFloatPtr());
  glUniform4fv(glslProgramDef_t::uniform_lightProjectionMatrixT, 1, din->lightProjection[1].ToFloatPtr());
  glUniform4fv(glslProgramDef_t::uniform_lightProjectionMatrixQ, 1, din->lightProjection[2].ToFloatPtr());
  glUniform4fv(glslProgramDef_t::uniform_lightFallOffS, 1, din->lightProjection[3].ToFloatPtr());

  glUniform4fv(glslProgramDef_t::uniform_bumpMatrixS, 1, din->bumpMatrix[0].ToFloatPtr());
  glUniform4fv(glslProgramDef_t::uniform_bumpMatrixT, 1, din->bumpMatrix[1].ToFloatPtr());
  glUniform4fv(glslProgramDef_t::uniform_diffuseMatrixS, 1, din->diffuseMatrix[0].ToFloatPtr());
  glUniform4fv(glslProgramDef_t::uniform_diffuseMatrixT, 1, din->diffuseMatrix[1].ToFloatPtr());
  glUniform4fv(glslProgramDef_t::uniform_specularMatrixS, 1, din->specularMatrix[0].ToFloatPtr());
  glUniform4fv(glslProgramDef_t::uniform_specularMatrixT, 1, din->specularMatrix[1].ToFloatPtr());

  static const float zero[4] = { 0, 0, 0, 0 };
  static const float one[4] = { 1, 1, 1, 1 };
  static const float negOne[4] = { -1, -1, -1, -1 };

  switch (din->vertexColor) {
  case SVC_IGNORE:
    glUniform4fv(glslProgramDef_t::uniform_color_modulate, 1, zero);
    glUniform4fv(glslProgramDef_t::uniform_color_add, 1, one);
    break;
  case SVC_MODULATE:
    glUniform4fv(glslProgramDef_t::uniform_color_modulate, 1, one);
    glUniform4fv(glslProgramDef_t::uniform_color_add, 1, zero);
    break;
  case SVC_INVERSE_MODULATE:
    glUniform4fv(glslProgramDef_t::uniform_color_modulate, 1, negOne);
    glUniform4fv(glslProgramDef_t::uniform_color_add, 1, one);
    break;
  }

  glUniform4fv(glslProgramDef_t::uniform_diffuse_color, 1, din->diffuseColor.ToFloatPtr());
  glUniform4fv(glslProgramDef_t::uniform_specular_color, 1, din->specularColor.ToFloatPtr());  
  
  glUniform1i(glslProgramDef_t::uniform_texture0, 0);
  glUniform1i(glslProgramDef_t::uniform_texture1, 1);
  glUniform1i(glslProgramDef_t::uniform_texture2, 2);
  glUniform1i(glslProgramDef_t::uniform_texture3, 3);
  glUniform1i(glslProgramDef_t::uniform_texture4, 4);
  glUniform1i(glslProgramDef_t::uniform_texture5, 5);
  glUniform1i(glslProgramDef_t::uniform_texture6, 6);
  glUniform1i(glslProgramDef_t::uniform_texture7, 7);

  // texture 1 will be the per-surface bump map
  GL_SelectTextureNoClient(1);
  din->bumpImage->Bind();

  // texture 2 will be the light falloff texture
  GL_SelectTextureNoClient(2);
  din->lightFalloffImage->Bind();

  // texture 3 will be the light projection texture
  GL_SelectTextureNoClient(3);
  din->lightImage->Bind();

  // texture 4 is the per-surface diffuse map
  GL_SelectTextureNoClient(4);
  din->diffuseImage->Bind();

  // texture 5 is the per-surface specular map
  GL_SelectTextureNoClient(5);
  din->specularImage->Bind();

  // draw it
  RB_DrawElementsWithCounters(din->surf->geo);
}

/*
=============
RB_GLSL_CreateDrawInteractions

=============
*/
void RB_GLSL_CreateDrawInteractions(const drawSurf_t *surf) {
  if (!surf) {
    return;
  }

  if(!interactionProgram) {
    interactionProgram = R_FindGlslProgram("interaction.vp", "interaction.fp");

    if(!interactionProgram) {
      return;
    }
  }

  // perform setup here that will be constant for all interactions
  GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc);  

  // bind the vertex program
  glDisable(GL_VERTEX_PROGRAM_ARB);
  glDisable(GL_FRAGMENT_PROGRAM_ARB);
  glUseProgram(interactionProgram->ident);

  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_position);
  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_texcoord);
  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_normal);
  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_color);
  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_binormal);
  glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_tangent);

  // texture 0 is the normalization cube map for the vector towards the light
  GL_SelectTextureNoClient(0);
  if (backEnd.vLight->lightShader->IsAmbientLight()) {
    globalImages->ambientNormalMap->Bind();
  }
  else {
    globalImages->normalCubeMapImage->Bind();
  }

  // texture 6 is the specular lookup table
  GL_SelectTextureNoClient(6);
  globalImages->specularTableImage->Bind();

  for (; surf; surf = surf->nextOnLight) {
    // perform setup here that will not change over multiple interaction passes

    // set the vertex pointers
    idDrawVert	*ac = (idDrawVert *)vertexCache.Position(surf->geo->ambientCache);

    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_position, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->xyz.ToFloatPtr());
    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_texcoord, 2, GL_FLOAT, false, sizeof(idDrawVert), ac->st.ToFloatPtr());
    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_normal, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->normal.ToFloatPtr());
    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_color, 4, GL_UNSIGNED_BYTE, false, sizeof(idDrawVert), (void *)&ac->color);
    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_binormal, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->tangents[1].ToFloatPtr());
    glVertexAttribPointer(glslProgramDef_t::vertex_attrib_tangent, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->tangents[0].ToFloatPtr());

    // this may cause RB_ARB2_DrawInteraction to be exacuted multiple
    // times with different colors and images if the surface or light have multiple layers
    RB_CreateSingleDrawInteractions(surf, RB_GLSL_DrawInteraction);
  }

  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_position);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_texcoord);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_normal);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_color);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_binormal);
  glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_tangent);

  // disable features
  GL_SelectTextureNoClient(6);
  globalImages->BindNull();

  GL_SelectTextureNoClient(5);
  globalImages->BindNull();

  GL_SelectTextureNoClient(4);
  globalImages->BindNull();

  GL_SelectTextureNoClient(3);
  globalImages->BindNull();

  GL_SelectTextureNoClient(2);
  globalImages->BindNull();

  GL_SelectTextureNoClient(1);
  globalImages->BindNull();

  backEnd.glState.currenttmu = -1;
  GL_SelectTexture(0);

  glUseProgram(0);
}



/*
==================
RB_GLSL_DrawInteractions
==================
*/
void RB_GLSL_DrawInteractions(void) {
  viewLight_t		*vLight;
  const idMaterial	*lightShader;

  GL_SelectTexture(0);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);

  //
  // for each light, perform adding and shadowing
  //
  for (vLight = backEnd.viewDef->viewLights; vLight; vLight = vLight->next) {
    backEnd.vLight = vLight;

    // do fogging later
    if (vLight->lightShader->IsFogLight()) {
      continue;
    }
    if (vLight->lightShader->IsBlendLight()) {
      continue;
    }

    if (!vLight->localInteractions && !vLight->globalInteractions
      && !vLight->translucentInteractions) {
      continue;
    }

    lightShader = vLight->lightShader;

    // clear the stencil buffer if needed
    if (vLight->globalShadows || vLight->localShadows) {
      backEnd.currentScissor = vLight->scissorRect;
      if (r_useScissor.GetBool()) {
        glScissor(backEnd.viewDef->viewport.x1 + backEnd.currentScissor.x1,
          backEnd.viewDef->viewport.y1 + backEnd.currentScissor.y1,
          backEnd.currentScissor.x2 + 1 - backEnd.currentScissor.x1,
          backEnd.currentScissor.y2 + 1 - backEnd.currentScissor.y1);
      }
      glClear(GL_STENCIL_BUFFER_BIT);
    }
    else {
      // no shadows, so no need to read or write the stencil buffer
      // we might in theory want to use GL_ALWAYS instead of disabling
      // completely, to satisfy the invarience rules
      glStencilFunc(GL_ALWAYS, 128, 255);
    }

    RB_GLSL_StencilShadowPass(vLight->globalShadows);
    RB_GLSL_CreateDrawInteractions(vLight->localInteractions);
    RB_GLSL_StencilShadowPass(vLight->localShadows);
    RB_GLSL_CreateDrawInteractions(vLight->globalInteractions);

    glUseProgram(0);

    // translucent surfaces never get stencil shadowed
    if (r_skipTranslucent.GetBool()) {
      continue;
    }

    glStencilFunc(GL_ALWAYS, 128, 255);

    backEnd.depthFunc = GLS_DEPTHFUNC_LESS;
    RB_GLSL_CreateDrawInteractions(vLight->translucentInteractions);

    backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
  }

  // disable stencil shadow test
  glStencilFunc(GL_ALWAYS, 128, 255);

  GL_SelectTexture(0);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
}
