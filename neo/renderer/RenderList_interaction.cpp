#include "tr_local.h"
#include "RenderList.h"
#include "RenderProgram.h"


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

		interactionList.Append( *din );
	}
}


/*
=============
RB_GLSL_CreateDrawInteractions

=============
*/
static void RB_GLSL_CreateDrawInteractions( const drawSurf_t *surf, InteractionList& interactionList ) {

	if (r_skipInteractions.GetBool()) {
		return;
	}

	for (; surf; surf = surf->nextOnLight) {
		const idMaterial	*surfaceShader = surf->material;
		const float			*surfaceRegs = surf->shaderRegisters;
		const viewLight_t	*vLight = backEnd.vLight;
		const idMaterial	*lightShader = vLight->lightShader;
		const float			*lightRegs = vLight->shaderRegisters;
		drawInteraction_t	inter;
		inter.hasBumpMatrix = inter.hasDiffuseMatrix = inter.hasSpecularMatrix = false;

		if (!surf->geo || !surf->geo->ambientCache) {
			continue;
		}

		inter.surf = surf;
		inter.lightFalloffImage = vLight->falloffImage;

		R_GlobalPointToLocal( surf->space->modelMatrix, vLight->globalLightOrigin, inter.localLightOrigin.ToVec3() );
		R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, inter.localViewOrigin.ToVec3() );
		inter.localLightOrigin[3] = 0;
		inter.localViewOrigin[3] = 1;
		inter.ambientLight = lightShader->IsAmbientLight();

		// the base projections may be modified by texture matrix on light stages
		idPlane lightProject[4];
		for (int i = 0; i < 4; i++) {
			R_GlobalPlaneToLocal( surf->space->modelMatrix, backEnd.vLight->lightProject[i], lightProject[i] );
		}

		for (int lightStageNum = 0; lightStageNum < lightShader->GetNumStages(); lightStageNum++) {
			const shaderStage_t	*lightStage = lightShader->GetStage( lightStageNum );

			// ignore stages that fail the condition
			if (!lightRegs[lightStage->conditionRegister]) {
				continue;
			}

			inter.lightImage = lightStage->texture.image;

			memcpy( inter.lightProjection, lightProject, sizeof(inter.lightProjection) );
			// now multiply the texgen by the light texture matrix
			if (lightStage->texture.hasMatrix) {
				RB_GetShaderTextureMatrix( lightRegs, &lightStage->texture, backEnd.lightTextureMatrix );
				RB_BakeTextureMatrixIntoTexgen( reinterpret_cast<class idPlane *>(inter.lightProjection), backEnd.lightTextureMatrix );
			}

			inter.bumpImage = NULL;
			inter.specularImage = NULL;
			inter.diffuseImage = NULL;
			inter.diffuseColor[0] = inter.diffuseColor[1] = inter.diffuseColor[2] = inter.diffuseColor[3] = 0;
			inter.specularColor[0] = inter.specularColor[1] = inter.specularColor[2] = inter.specularColor[3] = 0;

			float lightColor[4];

			// backEnd.lightScale is calculated so that lightColor[] will never exceed
			// tr.backEndRendererMaxLight
			lightColor[0] = backEnd.lightScale * lightRegs[lightStage->color.registers[0]];
			lightColor[1] = backEnd.lightScale * lightRegs[lightStage->color.registers[1]];
			lightColor[2] = backEnd.lightScale * lightRegs[lightStage->color.registers[2]];
			lightColor[3] = lightRegs[lightStage->color.registers[3]];

			// go through the individual stages
			for (int surfaceStageNum = 0; surfaceStageNum < surfaceShader->GetNumStages(); surfaceStageNum++) {
				const shaderStage_t	*surfaceStage = surfaceShader->GetStage( surfaceStageNum );

				switch (surfaceStage->lighting) {
				case SL_AMBIENT: {
					// ignore ambient stages while drawing interactions
					break;
				}
				case SL_BUMP: {
					// ignore stage that fails the condition
					if (!surfaceRegs[surfaceStage->conditionRegister]) {
						break;
					}
					// draw any previous interaction
					RB_SubmittInteraction( &inter, interactionList );
					inter.diffuseImage = NULL;
					inter.specularImage = NULL;
					R_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.bumpImage, inter.bumpMatrix, NULL );
					inter.hasBumpMatrix = surfaceStage->texture.hasMatrix;
					break;
				}
				case SL_DIFFUSE: {
					// ignore stage that fails the condition
					if (!surfaceRegs[surfaceStage->conditionRegister]) {
						break;
					}
					if (inter.diffuseImage) {
						RB_SubmittInteraction( &inter, interactionList );
					}
					R_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.diffuseImage,
						inter.diffuseMatrix, inter.diffuseColor.ToFloatPtr() );
					inter.diffuseColor[0] *= lightColor[0];
					inter.diffuseColor[1] *= lightColor[1];
					inter.diffuseColor[2] *= lightColor[2];
					inter.diffuseColor[3] *= lightColor[3];
					inter.vertexColor = surfaceStage->vertexColor;
					inter.hasDiffuseMatrix = surfaceStage->texture.hasMatrix;
					break;
				}
				case SL_SPECULAR: {
					// ignore stage that fails the condition
					if (!surfaceRegs[surfaceStage->conditionRegister]) {
						break;
					}
					if (inter.specularImage) {
						RB_SubmittInteraction( &inter, interactionList );
					}
					R_SetDrawInteraction( surfaceStage, surfaceRegs, &inter.specularImage,
						inter.specularMatrix, inter.specularColor.ToFloatPtr() );
					inter.specularColor[0] *= lightColor[0];
					inter.specularColor[1] *= lightColor[1];
					inter.specularColor[2] *= lightColor[2];
					inter.specularColor[3] *= lightColor[3];
					inter.specularColor *= r_specularScale.GetFloat();
					inter.vertexColor = surfaceStage->vertexColor;
					inter.hasSpecularMatrix = surfaceStage->texture.hasMatrix;
					break;
				}
				}
			}

			// draw the final interaction
			RB_SubmittInteraction( &inter, interactionList );
		}
	}
}

static void RB_GLSL_SubmitDrawInteractions( const InteractionList& interactionList ) {
	if (interactionList.IsEmpty())
		return;

	// perform setup here that will be constant for all interactions
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	GL_UseProgram( interactionProgram );

	fhRenderProgram::SetShading( r_shading.GetInteger() );
	fhRenderProgram::SetSpecularExp( r_specularExp.GetFloat() );

	if (backEnd.vLight->lightDef->ShadowMode() == shadowMode_t::ShadowMap) {
		const idVec4 globalLightOrigin = idVec4( backEnd.vLight->globalLightOrigin, 1 );
		fhRenderProgram::SetGlobalLightOrigin( globalLightOrigin );

		const float shadowBrightness = backEnd.vLight->lightDef->ShadowBrightness();
		const float shadowSoftness = backEnd.vLight->lightDef->ShadowSoftness();
		fhRenderProgram::SetShadowParams( idVec4( shadowSoftness, shadowBrightness, backEnd.vLight->nearClip[0], backEnd.vLight->farClip[0] ) );

		if(backEnd.vLight->lightDef->parms.parallel) {
			//parallel light
			fhRenderProgram::SetShadowMappingMode( 3 );
			fhRenderProgram::SetPointLightProjectionMatrices( backEnd.vLight->viewProjectionMatrices[0].ToFloatPtr() );
			fhRenderProgram::SetShadowCoords( backEnd.vLight->shadowCoords, 6 );
			fhRenderProgram::SetCascadeDistances(
				r_smCascadeDistance0.GetFloat(),
				r_smCascadeDistance1.GetFloat(),
				r_smCascadeDistance2.GetFloat(),
				r_smCascadeDistance3.GetFloat(),
				r_smCascadeDistance4.GetFloat());

			idVec4 shadowmapSizes[6] = {
				idVec4(backEnd.vLight->nearClip[0], backEnd.vLight->farClip[0], backEnd.vLight->width[0], backEnd.vLight->height[0]),
				idVec4(backEnd.vLight->nearClip[1], backEnd.vLight->farClip[1], backEnd.vLight->width[1], backEnd.vLight->height[1]),
				idVec4(backEnd.vLight->nearClip[2], backEnd.vLight->farClip[2], backEnd.vLight->width[2], backEnd.vLight->height[2]),
				idVec4(backEnd.vLight->nearClip[3], backEnd.vLight->farClip[3], backEnd.vLight->width[3], backEnd.vLight->height[3]),
				idVec4(backEnd.vLight->nearClip[4], backEnd.vLight->farClip[4], backEnd.vLight->width[4], backEnd.vLight->height[4]),
				idVec4(backEnd.vLight->nearClip[5], backEnd.vLight->farClip[5], backEnd.vLight->width[5], backEnd.vLight->height[5])
			};

			fhRenderProgram::SetShadowMapSize(shadowmapSizes, 6);
		}
		else if (backEnd.vLight->lightDef->parms.pointLight) {
			//point light
			fhRenderProgram::SetShadowMappingMode( 1 );
			fhRenderProgram::SetPointLightProjectionMatrices( backEnd.vLight->viewProjectionMatrices[0].ToFloatPtr() );
			fhRenderProgram::SetShadowCoords(backEnd.vLight->shadowCoords, 6);

			{
				const idMat3 axis = backEnd.vLight->lightDef->parms.axis;

				float viewerMatrix[16];

				viewerMatrix[0] = axis[0][0];
				viewerMatrix[4] = axis[0][1];
				viewerMatrix[8] = axis[0][2];
				viewerMatrix[12] = 0;

				viewerMatrix[1] = axis[1][0];
				viewerMatrix[5] = axis[1][1];
				viewerMatrix[9] = axis[1][2];
				viewerMatrix[13] = 0;

				viewerMatrix[2] = axis[2][0];
				viewerMatrix[6] = axis[2][1];
				viewerMatrix[10] = axis[2][2];
				viewerMatrix[14] = 0;

				viewerMatrix[3] = 0;
				viewerMatrix[7] = 0;
				viewerMatrix[11] = 0;
				viewerMatrix[15] = 1;

				fhRenderProgram::SetInverseLightRotation( viewerMatrix );
			}
		}
		else {
			//projected light
			fhRenderProgram::SetShadowMappingMode( 2 );
			fhRenderProgram::SetSpotLightProjectionMatrix( backEnd.vLight->viewProjectionMatrices[0].ToFloatPtr() );
			fhRenderProgram::SetShadowCoords(backEnd.vLight->shadowCoords, 1);
		}
	}
	else {
		//no shadows
		fhRenderProgram::SetShadowMappingMode( 0 );
	}

	//make sure depth hacks are disabled
	//FIXME(johl): why is (sometimes) a depth hack enabled at this point?
	RB_LeaveDepthHack();

	fhRenderProgram::SetProjectionMatrix( backEnd.viewDef->projectionMatrix );
	fhRenderProgram::SetPomMaxHeight( -1 );

	const viewEntity_t* currentSpace = nullptr;
	stageVertexColor_t currentVertexColor = (stageVertexColor_t)-1;
	bool currentPomEnabled = false;
	idScreenRect currentScissor;
	bool depthHackActive = false;
	bool currentHasBumpMatrix = false;
	bool currentHasDiffuseMatrix = false;
	bool currentHasSpecularMatrix = false;
	idVec4 currentDiffuseColor = idVec4( 1, 1, 1, 1 );
	idVec4 currentSpecularColor = idVec4( 1, 1, 1, 1 );

	fhRenderProgram::SetDiffuseColor( currentDiffuseColor );
	fhRenderProgram::SetSpecularColor( currentSpecularColor );
	fhRenderProgram::SetBumpMatrix( idVec4::identityS, idVec4::identityT );
	fhRenderProgram::SetSpecularMatrix( idVec4::identityS, idVec4::identityT );
	fhRenderProgram::SetDiffuseMatrix( idVec4::identityS, idVec4::identityT );


	if (r_useScissor.GetBool()) {
		glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		currentScissor.x1 = 0;
		currentScissor.y1 = 0;
		currentScissor.x2 = glConfig.vidWidth;
		currentScissor.y2 = glConfig.vidHeight;
	}

	const int num = interactionList.Num();
	for (int i = 0; i < num; ++i) {
		const auto& din = interactionList[i];

		const auto offset = vertexCache.Bind( din.surf->geo->ambientCache );
		GL_SetupVertexAttributes( fhVertexLayout::Draw, offset );

		if (currentSpace != din.surf->space) {
			fhRenderProgram::SetModelMatrix( din.surf->space->modelMatrix );
			fhRenderProgram::SetModelViewMatrix( din.surf->space->modelViewMatrix );

			if (din.surf->space->modelDepthHack) {
				RB_EnterModelDepthHack( din.surf->space->modelDepthHack );
				fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );
				depthHackActive = true;
			}
			else if (din.surf->space->weaponDepthHack) {
				RB_EnterWeaponDepthHack();
				fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );
				depthHackActive = true;
			}
			else if (depthHackActive) {
				RB_LeaveDepthHack();
				fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );				
				depthHackActive = false;
			}

			// change the scissor if needed
			if (r_useScissor.GetBool() && !currentScissor.Equals( din.surf->scissorRect )) {
				currentScissor = din.surf->scissorRect;
				glScissor( backEnd.viewDef->viewport.x1 + currentScissor.x1,
					backEnd.viewDef->viewport.y1 + currentScissor.y1,
					currentScissor.x2 + 1 - currentScissor.x1,
					currentScissor.y2 + 1 - currentScissor.y1 );
			}

			currentSpace = din.surf->space;
		}

		fhRenderProgram::SetLocalLightOrigin( din.localLightOrigin );
		fhRenderProgram::SetLocalViewOrigin( din.localViewOrigin );
		fhRenderProgram::SetLightProjectionMatrix( din.lightProjection[0], din.lightProjection[1], din.lightProjection[2] );
		fhRenderProgram::SetLightFallOff( din.lightProjection[3] );

		if (din.hasBumpMatrix) {
			fhRenderProgram::SetBumpMatrix( din.bumpMatrix[0], din.bumpMatrix[1] );
			currentHasBumpMatrix = true;
		}
		else if (currentHasBumpMatrix) {
			fhRenderProgram::SetBumpMatrix( idVec4::identityS, idVec4::identityT );
			currentHasBumpMatrix = false;
		}

		if (din.hasDiffuseMatrix) {
			fhRenderProgram::SetDiffuseMatrix( din.diffuseMatrix[0], din.diffuseMatrix[1] );
			currentHasDiffuseMatrix = true;
		}
		else if (currentHasDiffuseMatrix) {
			fhRenderProgram::SetDiffuseMatrix( idVec4::identityS, idVec4::identityT );
			currentHasDiffuseMatrix = false;
		}

		if (din.hasSpecularMatrix) {
			fhRenderProgram::SetSpecularMatrix( din.specularMatrix[0], din.specularMatrix[1] );
			currentHasSpecularMatrix = true;
		}
		else if (currentHasSpecularMatrix) {
			fhRenderProgram::SetSpecularMatrix( idVec4::identityS, idVec4::identityT );
			currentHasSpecularMatrix = false;
		}

		if (currentVertexColor != din.vertexColor) {
			switch (din.vertexColor) {
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
			currentVertexColor = din.vertexColor;
		}

		if (din.diffuseColor != currentDiffuseColor) {
			fhRenderProgram::SetDiffuseColor( din.diffuseColor );
			currentDiffuseColor = din.diffuseColor;
		}

		if (din.specularColor != currentSpecularColor) {
			fhRenderProgram::SetSpecularColor( din.specularColor );
			currentSpecularColor = din.specularColor;
		}

		const bool pomEnabled = r_pomEnabled.GetBool() && din.specularImage->hasAlpha;
		if (pomEnabled != currentPomEnabled) {
			if (pomEnabled) {
				fhRenderProgram::SetPomMaxHeight( r_pomMaxHeight.GetFloat() );
			}
			else {
				fhRenderProgram::SetPomMaxHeight( -1 );
			}
		}

		din.bumpImage->Bind( 1 );
		din.lightFalloffImage->Bind( 2 );
		din.lightImage->Bind( 3 );
		din.diffuseImage->Bind( 4 );
		din.specularImage->Bind( 5 );

		// draw it
		backEnd.stats.groups[backEndGroup::Interaction].drawcalls += 1;
		backEnd.stats.groups[backEndGroup::Interaction].tris += din.surf->geo->numIndexes / 3;
		RB_DrawElementsWithCounters( din.surf->geo );
	}

	if (depthHackActive) {
		RB_LeaveDepthHack();
		fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );
	}

	if (r_useScissor.GetBool()) {
		glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		backEnd.currentScissor.x1 = 0;
		backEnd.currentScissor.y1 = 0;
		backEnd.currentScissor.x2 = glConfig.vidWidth;
		backEnd.currentScissor.y2 = glConfig.vidHeight;
	}
}

void InteractionList::AddDrawSurfacesOnLight( const drawSurf_t *surf ) {
	RB_GLSL_CreateDrawInteractions(surf, *this);
}

void InteractionList::Submit() {
	RB_GLSL_SubmitDrawInteractions(*this);
}