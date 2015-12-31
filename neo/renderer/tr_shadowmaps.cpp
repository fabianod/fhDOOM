#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_local.h"
#include "IndexCache.h"

idCVar r_smObjectCulling( "r_smObjectCulling", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "cull objects/surfaces that are outside the shadow/light frustum when rendering shadow maps" );
idCVar r_smFaceCullMode( "r_smFaceCullMode", "2", CVAR_RENDERER|CVAR_INTEGER | CVAR_ARCHIVE, "Determines which faces should be rendered to shadow map: 0=front, 1=back, 2=front-and-back");
idCVar r_smFov( "r_smFov", "93", CVAR_RENDERER|CVAR_FLOAT | CVAR_ARCHIVE, "fov used when rendering point light shadow maps");
idCVar r_smFarClip( "r_smFarClip", "-1", CVAR_RENDERER|CVAR_INTEGER | CVAR_ARCHIVE, "far clip distance for rendering shadow maps: -1=infinite-z, 0=max light radius, other=fixed distance");
idCVar r_smNearClip( "r_smNearClip", "4", CVAR_RENDERER|CVAR_INTEGER | CVAR_ARCHIVE, "near clip distance for rendering shadow maps");
idCVar r_smUseStaticOccluderModel( "r_smUseStaticOccluderModel", "1", CVAR_RENDERER | CVAR_BOOL | CVAR_ARCHIVE, "the occluder model is a single surface merged from all static and opaque world surfaces. Can be rendered to the shadow map with a single draw call");

static const int CULL_RECEIVER = 1;	// still draw occluder, but it is out of the view
static const int CULL_OCCLUDER_AND_RECEIVER = 2;	// the surface doesn't effect the view at all

static float viewLightAxialSize;

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

static float	s_flipMatrix[16] = {
	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	0, 0, -1, 0,
	-1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 0, 1
};

template<typename T>
static const void* attributeOffset(T offset, const void* attributeOffset)
{
	return reinterpret_cast<const void*>((std::ptrdiff_t)offset + (std::ptrdiff_t)attributeOffset);
}

static void RB_CreateShadowMapProjectionMatrix(const viewLight_t* vLight, float* m) {

	memset(m, 0, sizeof(m[0]) * 16);
	//
	// set up 90 degree projection matrix
	//
	const float	fov = r_smFov.GetFloat();
	const float zNear = r_smNearClip.GetInteger();

	if (r_smFarClip.GetInteger() < 0) {
		const float ymax = zNear * tan(fov * idMath::PI / 360.0f);
		const float ymin = -ymax;
		const float xmax = zNear * tan(fov * idMath::PI / 360.0f);
		const float xmin = -xmax;
		const float width = xmax - xmin;
		const float height = ymax - ymin;

		m[0] = 2 * zNear / width;
		m[5] = 2 * zNear / height;

		// this is the far-plane-at-infinity formulation, and
		// crunches the Z range slightly so w=0 vertexes do not
		// rasterize right at the wraparound point
		m[10] = -0.999f;
		m[11] = -1;
		m[14] = -2.0f * zNear;
	}
	else if (r_smFarClip.GetInteger() == 0) {
		const float zFar = vLight->lightDef->GetMaximumCenterToEdgeDistance();
		const float D2R = idMath::PI / 180.0;
		const float scale = 1.0 / tan(D2R * fov / 2);
		const float nearmfar = zNear - zFar;

		m[0] = scale;
		m[5] = scale;
		m[10] = (zFar + zNear) / nearmfar;
		m[11] = -1;
		m[14] = 2 * zFar*zNear / nearmfar;
	}
	else {
		const float zFar = r_smFarClip.GetInteger();
		const float D2R = idMath::PI / 180.0;
		const float scale = 1.0 / tan(D2R * fov / 2);
		const float nearmfar = zNear - zFar;

		m[0] = scale;
		m[5] = scale;
		m[10] = (zFar + zNear) / nearmfar;
		m[11] = -1;
		m[14] = 2 * zFar*zNear / nearmfar;
	}
}

static void RB_CreateShadowViewMatrix(const viewLight_t* vLight, int side, float* viewMatrix) {
	memset( viewMatrix, 0, 16 * sizeof(viewMatrix[0]) );

	if (side == -1) {
		// projected light
		idVec3 vec = vLight->lightDef->parms.target;
		vec.Normalize();
		viewMatrix[0] = vec[0];
		viewMatrix[4] = vec[1];
		viewMatrix[8] = vec[2];

		vec = vLight->lightDef->parms.right;
		vec.Normalize();
		viewMatrix[1] = -vec[0];
		viewMatrix[5] = -vec[1];
		viewMatrix[9] = -vec[2];

		vec = vLight->lightDef->parms.up;
		vec.Normalize();
		viewMatrix[2] = vec[0];
		viewMatrix[6] = vec[1];
		viewMatrix[10] = vec[2];
	}
	else {
		// side of a point light		
		switch (side) {
		case 0:
			viewMatrix[0] = 1;
			viewMatrix[9] = 1;
			viewMatrix[6] = -1;
			break;
		case 1:
			viewMatrix[0] = -1;
			viewMatrix[9] = -1;
			viewMatrix[6] = -1;
			break;
		case 2:
			viewMatrix[4] = 1;
			viewMatrix[1] = -1;
			viewMatrix[10] = 1;
			break;
		case 3:
			viewMatrix[4] = -1;
			viewMatrix[1] = -1;
			viewMatrix[10] = -1;
			break;
		case 4:
			viewMatrix[8] = 1;
			viewMatrix[1] = -1;
			viewMatrix[6] = -1;
			break;
		case 5:
			viewMatrix[8] = -1;
			viewMatrix[1] = 1;
			viewMatrix[6] = -1;
			break;
		}
	}

	idVec3	origin = vLight->lightDef->globalLightOrigin;
	viewMatrix[12] = -origin[0] * viewMatrix[0] + -origin[1] * viewMatrix[4] + -origin[2] * viewMatrix[8];
	viewMatrix[13] = -origin[0] * viewMatrix[1] + -origin[1] * viewMatrix[5] + -origin[2] * viewMatrix[9];
	viewMatrix[14] = -origin[0] * viewMatrix[2] + -origin[1] * viewMatrix[6] + -origin[2] * viewMatrix[10];

	viewMatrix[3] = 0;
	viewMatrix[7] = 0;
	viewMatrix[11] = 0;
	viewMatrix[15] = 1;
}

void RB_GLSL_GetShadowParams(float* minBias, float* maxBias, float* fuzzyness, int* samples) {
	assert(minBias);
	assert(maxBias);
	assert(fuzzyness);
	assert(samples);

	static const struct {
		float size;
		float minBias;
		float maxBias;
		float fuzzyness;
		int   samples;
	} biasTable[] = {
		{ 0,    0.0001, 0.01, 3.5, 3 },
		{ 150,  0.0005,  0.006, 2.8, 3 },
		{ 300,  0.000001, 0.0001, 2, 3 },
		{ 1000, 0.000001, 0.000004, 1.2, 4 },
	};

	//const float lightSize = max(backEnd.vLight->lightDef->parms.lightRadius.x, max(backEnd.vLight->lightDef->parms.lightRadius.y, backEnd.vLight->lightDef->parms.lightRadius.z));
	const float lightSize = backEnd.vLight->lightDef->GetMaximumCenterToEdgeDistance();

	for (int i = 0; i < sizeof(biasTable) / sizeof(biasTable[0]); ++i) {
		const auto& a = biasTable[i];
		if (lightSize >= a.size) {
			*minBias = a.minBias;
			*maxBias = a.maxBias;
			*fuzzyness = a.fuzzyness;
			*samples = a.samples;
		}
		else {
			break;
		}
	}
}


/*
==================
R_EXP_CalcLightAxialSize

all light side projections must currently match, so non-centered
and non-cubic lights must take the largest length
==================
*/
float	R_EXP_CalcLightAxialSize(const viewLight_t *vLight) {
	float	max = 0;

	if (!vLight->lightDef->parms.pointLight) {
		idVec3	dir = vLight->lightDef->parms.target - vLight->lightDef->parms.origin;
		max = dir.Length();
		return max;
	}

	for (int i = 0; i < 3; i++) {
		float	dist = fabs(vLight->lightDef->parms.lightCenter[i]);
		dist += vLight->lightDef->parms.lightRadius[i];
		if (dist > max) {
			max = dist;
		}
	}
	return max;
}

/*
==================
RB_EXP_CullInteractions

Sets surfaceInteraction_t->cullBits
==================
*/
void RB_EXP_CullInteractions(viewLight_t *vLight, idPlane frustumPlanes[6]) {
	
	for (idInteraction *inter = vLight->lightDef->firstInteraction; inter; inter = inter->lightNext) {
		const idRenderEntityLocal *entityDef = inter->entityDef;

		if (!entityDef) {
			continue;
		}
		if (inter->numSurfaces < 1) {
			continue;
		}

		int	culled = 0;

		if (r_smObjectCulling.GetBool()) {

			// transform light frustum into object space, positive side points outside the light
			idPlane	localPlanes[6];
			int		plane;
			for (plane = 0; plane < 6; plane++) {
				R_GlobalPlaneToLocal(entityDef->modelMatrix, frustumPlanes[plane], localPlanes[plane]);
			}

			// cull the entire entity bounding box
			// has referenceBounds been tightened to the actual model bounds?
			idVec3	corners[8];
			for (int i = 0; i < 8; i++) {
				corners[i][0] = entityDef->referenceBounds[i & 1][0];
				corners[i][1] = entityDef->referenceBounds[(i >> 1) & 1][1];
				corners[i][2] = entityDef->referenceBounds[(i >> 2) & 1][2];
			}

			for (plane = 0; plane < 6; plane++) {
				int		j;
				for (j = 0; j < 8; j++) {
					// if a corner is on the negative side (inside) of the frustum, the surface is not culled
					// by this plane
					if (corners[j] * localPlanes[plane].ToVec4().ToVec3() + localPlanes[plane][3] < 0) {
						break;
					}
				}
				if (j == 8) {
					break;			// all points outside the light
				}
			}
			if (plane < 6) {
				culled = CULL_OCCLUDER_AND_RECEIVER;
			}
		}

		for (int i = 0; i < inter->numSurfaces; i++) {
			surfaceInteraction_t	*surfInt = &inter->surfaces[i];

			

			if (!surfInt->ambientTris) {
				continue;
			}
			surfInt->expCulled = culled;
		}

	}
}

static void RB_RenderShadowCasters(const viewLight_t *vLight, const float* shadowViewMatrix) {	
	
	glUniformMatrix4fv( glslProgramDef_t::uniform_projectionMatrix, 1, false, GL_ProjectionMatrix.Top() );

	for (idInteraction *inter = vLight->lightDef->firstInteraction; inter; inter = inter->lightNext) {
		const idRenderEntityLocal *entityDef = inter->entityDef;

		if (!entityDef) {
			continue;
		}
		if (inter->numSurfaces < 1) {
			continue;
		}

		bool staticOccluderModelWasRendered = false;
		if(r_smUseStaticOccluderModel.GetBool() && entityDef->staticOccluderModel) {
			assert(entityDef->staticOccluderModel->NumSurfaces() > 0);

			srfTriangles_t* tri = entityDef->staticOccluderModel->Surface(0)->geometry;

			if (!tri->ambientCache) {
				if (!R_CreateAmbientCache( const_cast<srfTriangles_t *>(tri), false )) {
					common->Error( "RB_RenderShadowCasters: Failed to alloc ambient cache" );
				}
			}

			if (r_ignore2.GetBool() && !tri->ambientIndexCache) {
				tri->ambientIndexCache = indexCache.Alloc( tri->indexes, tri->numIndexes * sizeof(tri->indexes[0]) );
			}

			const auto offset = vertexCache.Bind( tri->ambientCache );			
			glVertexAttribPointer( glslProgramDef_t::vertex_attrib_position, 3, GL_FLOAT, false, sizeof(idDrawVert), attributeOffset( offset, idDrawVert::xyzOffset ) );
			glVertexAttribPointer( glslProgramDef_t::vertex_attrib_texcoord, 2, GL_FLOAT, false, sizeof(idDrawVert), attributeOffset( offset, idDrawVert::texcoordOffset ) );
			glUniformMatrix4fv(glslProgramDef_t::uniform_modelViewMatrix, 1, false, shadowViewMatrix);
			glUniform1i(glslProgramDef_t::uniform_alphaTestEnabled, 0);

			if(r_ignore2.GetBool()) {
				indexCache.Bind(tri->ambientIndexCache);
				glDrawElements( GL_TRIANGLES,
					tri->numIndexes,
					GL_INDEX_TYPE,
					(void*)tri->ambientIndexCache->offset);
				indexCache.Unbind();
			} else 	{
				RB_DrawElementsWithCounters( tri );
			}

			backEnd.pc.c_shadowMapDraws++;
			staticOccluderModelWasRendered = true;
		}		

		float matrix[16];
		bool matrixOk = false; 		

		// draw each surface
		for (int i = 0; i < inter->numSurfaces; i++) {
			surfaceInteraction_t	*surfInt = &inter->surfaces[i];

			if (!surfInt->ambientTris) {
				continue;
			}

			const idMaterial* shader = surfInt->shader;

			if (shader && !shader->SurfaceCastsShadow() && shader->Coverage() != MC_PERFORATED) {
				continue;
			}


			// cull it
			if (surfInt->expCulled == CULL_OCCLUDER_AND_RECEIVER) {
				continue;
			}

			// render it
			const srfTriangles_t *tri = surfInt->ambientTris;
			if(!tri || !tri->numVerts) {
				continue;
			}

			if (staticOccluderModelWasRendered && shader->Coverage() == MC_OPAQUE)
				continue;


			if (!tri->ambientCache) {
				R_CreateAmbientCache(const_cast<srfTriangles_t *>(tri), false);
			}

			if(!matrixOk) {
				myGlMultMatrix( inter->entityDef->modelMatrix, shadowViewMatrix, matrix );
				matrixOk = true;
			}

			const auto offset = vertexCache.Bind(tri->ambientCache);
			glVertexAttribPointer(glslProgramDef_t::vertex_attrib_position, 3, GL_FLOAT, false, sizeof(idDrawVert), attributeOffset(offset, idDrawVert::xyzOffset));
			glVertexAttribPointer(glslProgramDef_t::vertex_attrib_texcoord, 2, GL_FLOAT, false, sizeof(idDrawVert), attributeOffset(offset, idDrawVert::texcoordOffset));
			glUniformMatrix4fv(glslProgramDef_t::uniform_modelViewMatrix, 1, false, matrix);		

			bool didDraw = false;
			
			// we may have multiple alpha tested stages
			if (shader->Coverage() == MC_PERFORATED) {
				// if the only alpha tested stages are condition register omitted,
				// draw a normal opaque surface
				
				float *regs = (float *)R_ClearedFrameAlloc(shader->GetNumRegisters() * sizeof(float));
				shader->EvaluateRegisters(regs, entityDef->parms.shaderParms, backEnd.viewDef, nullptr);


				// perforated surfaces may have multiple alpha tested stages
				for (int stage = 0; stage < shader->GetNumStages(); stage++) {
					const shaderStage_t* pStage = shader->GetStage(stage);

					if (!pStage->hasAlphaTest) {
						continue;
					}

					if (regs[pStage->conditionRegister] == 0) {
						continue;
					}

					// bind the texture
					pStage->texture.image->Bind();

					glUniform1i(glslProgramDef_t::uniform_alphaTestEnabled, 1);
					glUniform1f(glslProgramDef_t::uniform_alphaTestThreshold, 0.5f);

					idVec4 textureMatrix[2] = {idVec4(1,0,0,0), idVec4(0,1,0,0)};
					if (pStage->texture.hasMatrix) {						
						RB_GetShaderTextureMatrix(regs, &pStage->texture, textureMatrix);
					}
					glUniform4fv(glslProgramDef_t::uniform_diffuseMatrixS, 1, textureMatrix[0].ToFloatPtr());
					glUniform4fv(glslProgramDef_t::uniform_diffuseMatrixT, 1, textureMatrix[1].ToFloatPtr());

					// draw it
					RB_DrawElementsWithCounters(tri);
					backEnd.pc.c_shadowMapDraws++;
					didDraw = true;
					break;
				}
			}
			
			if(!didDraw) {
				glUniform1i(glslProgramDef_t::uniform_alphaTestEnabled, 0);
				// draw it
				RB_DrawElementsWithCounters(tri);
				backEnd.pc.c_shadowMapDraws++;
			}
		}
	}
}

static void RB_RenderShadowBuffer(viewLight_t* vLight, int side) {
	float	viewMatrix[16];
	RB_CreateShadowViewMatrix( vLight, side, viewMatrix );

	float lightProjectionMatrix[16];
	RB_CreateShadowMapProjectionMatrix(vLight, lightProjectionMatrix);

	fhFramebuffer* framebuffer = globalImages->shadowmapFramebuffer[side];
	framebuffer->Bind();

	GL_ProjectionMatrix.Push();
	GL_ProjectionMatrix.Load(lightProjectionMatrix);

	glViewport(0, 0, framebuffer->GetWidth(), framebuffer->GetHeight());
	glScissor(0, 0, framebuffer->GetWidth(), framebuffer->GetHeight());

	glStencilFunc(GL_ALWAYS, 0, 255);

	GL_State(GLS_DEPTHFUNC_LESS | GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO);	// make sure depth mask is off before clear
	glDepthMask(GL_TRUE);
	glEnable(GL_DEPTH_TEST);

	glClearDepth(1.0);
	glClear(GL_DEPTH_BUFFER_BIT);

	// draw all the occluders	

	backEnd.currentSpace = NULL;

	
	if(r_smObjectCulling.GetBool()) {
		// create frustum planes
		idPlane	globalFrustum[6];
		idVec3	origin = vLight->lightDef->globalLightOrigin;

		// near clip
		globalFrustum[0][0] = -viewMatrix[0];
		globalFrustum[0][1] = -viewMatrix[4];
		globalFrustum[0][2] = -viewMatrix[8];
		globalFrustum[0][3] = -(origin[0] * globalFrustum[0][0] + origin[1] * globalFrustum[0][1] + origin[2] * globalFrustum[0][2]);

		// far clip
		globalFrustum[1][0] = viewMatrix[0];
		globalFrustum[1][1] = viewMatrix[4];
		globalFrustum[1][2] = viewMatrix[8];
		globalFrustum[1][3] = -globalFrustum[0][3] - viewLightAxialSize;

		// side clips
		globalFrustum[2][0] = -viewMatrix[0] + viewMatrix[1];
		globalFrustum[2][1] = -viewMatrix[4] + viewMatrix[5];
		globalFrustum[2][2] = -viewMatrix[8] + viewMatrix[9];

		globalFrustum[3][0] = -viewMatrix[0] - viewMatrix[1];
		globalFrustum[3][1] = -viewMatrix[4] - viewMatrix[5];
		globalFrustum[3][2] = -viewMatrix[8] - viewMatrix[9];

		globalFrustum[4][0] = -viewMatrix[0] + viewMatrix[2];
		globalFrustum[4][1] = -viewMatrix[4] + viewMatrix[6];
		globalFrustum[4][2] = -viewMatrix[8] + viewMatrix[10];

		globalFrustum[5][0] = -viewMatrix[0] - viewMatrix[2];
		globalFrustum[5][1] = -viewMatrix[4] - viewMatrix[6];
		globalFrustum[5][2] = -viewMatrix[8] - viewMatrix[10];

		// is this nromalization necessary?
		for (int i = 0; i < 6; i++) {
			globalFrustum[i].ToVec4().ToVec3().Normalize();
		}

		for (int i = 2; i < 6; i++) {
			globalFrustum[i][3] = -(origin * globalFrustum[i].ToVec4().ToVec3());
		}

		RB_EXP_CullInteractions(vLight, globalFrustum);
	}

	float flippedViewMatrix[16];
	myGlMultMatrix( viewMatrix, s_flipMatrix, flippedViewMatrix );
	myGlMultMatrix(flippedViewMatrix, lightProjectionMatrix, &backEnd.shadowViewProjection[side][0]);
	RB_RenderShadowCasters(vLight, flippedViewMatrix);

	GL_ProjectionMatrix.Pop();

	backEnd.pc.c_shadowPasses++;
}


void RB_RenderShadowMaps(viewLight_t* vLight) {

	const idMaterial	*lightShader = vLight->lightShader;

	// do fogging later
	if (lightShader->IsFogLight()) {
		return;
	}
	if (lightShader->IsBlendLight()) {
		return;
	}

	if (!vLight->localInteractions && !vLight->globalInteractions
		&& !vLight->translucentInteractions) {
		return;
	}

	if(!vLight->lightShader->LightCastsShadows()) {
		return;
	}

	if (!vLight->frustumTris->ambientCache) {
		R_CreateAmbientCache(const_cast<srfTriangles_t *>(vLight->frustumTris), false);
	}

	// all light side projections must currently match, so non-centered
	// and non-cubic lights must take the largest length
	viewLightAxialSize = R_EXP_CalcLightAxialSize(vLight);

	GL_UseProgram(depthProgram);
	glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_position);
	glEnableVertexAttribArray(glslProgramDef_t::vertex_attrib_texcoord);

	switch (r_smFaceCullMode.GetInteger())
	{
	case 0:
		glEnable(GL_CULL_FACE);
		glFrontFace(GL_CCW);
		break;
	case 1:
		glEnable(GL_CULL_FACE);
		glFrontFace(GL_CW);
		break;
	case 2:
	default:
		glDisable(GL_CULL_FACE);
		break;
	}

	for (int side=0; side < 6; side++) {
		// FIXME: check for frustums completely off the screen

		// render a shadow buffer
		RB_RenderShadowBuffer(vLight, side);		
	}

	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CCW);
	glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_position);
	glDisableVertexAttribArray(glslProgramDef_t::vertex_attrib_texcoord);
	GL_UseProgram(nullptr);

	globalImages->defaultFramebuffer->Bind();
	backEnd.currentSpace = NULL;

	//reset viewport 
	glViewport(tr.viewportOffset[0] + backEnd.viewDef->viewport.x1,
		tr.viewportOffset[1] + backEnd.viewDef->viewport.y1,
		backEnd.viewDef->viewport.x2 + 1 - backEnd.viewDef->viewport.x1,
		backEnd.viewDef->viewport.y2 + 1 - backEnd.viewDef->viewport.y1);

	// the scissor may be smaller than the viewport for subviews
	glScissor(tr.viewportOffset[0] + backEnd.viewDef->viewport.x1 + backEnd.viewDef->scissor.x1,
		tr.viewportOffset[1] + backEnd.viewDef->viewport.y1 + backEnd.viewDef->scissor.y1,
		backEnd.viewDef->scissor.x2 + 1 - backEnd.viewDef->scissor.x1,
		backEnd.viewDef->scissor.y2 + 1 - backEnd.viewDef->scissor.y1);
		backEnd.currentScissor = backEnd.viewDef->scissor;


	// texture 5 is the per-surface specular map
	GL_SelectTextureNoClient(6);
	globalImages->shadowmapImage[0]->Bind();

	GL_SelectTextureNoClient(7);
	globalImages->shadowmapImage[1]->Bind();

	GL_SelectTextureNoClient(8);
	globalImages->shadowmapImage[2]->Bind();

	GL_SelectTextureNoClient(9);
	globalImages->shadowmapImage[3]->Bind();

	GL_SelectTextureNoClient(10);
	globalImages->shadowmapImage[4]->Bind();

	GL_SelectTextureNoClient(11);
	globalImages->shadowmapImage[5]->Bind();
}