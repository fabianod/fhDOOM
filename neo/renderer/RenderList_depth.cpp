#include "tr_local.h"
#include "RenderList.h"
#include "RenderProgram.h"

static void RB_GLSL_SubmitFillDepthRenderList( const DepthRenderList& renderlist ) {
	assert( depthProgram );

	// if we are just doing 2D rendering, no need to fill the depth buffer
	if (!backEnd.viewDef->viewEntitys) {
		return;
	}

	RB_LogComment( "---------- RB_GLSL_SubmitFillDepthRenderList ----------\n" );

	// enable the second texture for mirror plane clipping if needed
	if (backEnd.viewDef->numClipPlanes) {
		globalImages->alphaNotchImage->Bind( 1 );
	}

	// decal surfaces may enable polygon offset
	glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() );

	GL_State( GLS_DEPTHFUNC_LESS );

	// Enable stencil test if we are going to be using it for shadows.
	// If we didn't do this, it would be legal behavior to get z fighting
	// from the ambient pass and the light passes.
	glEnable( GL_STENCIL_TEST );
	glStencilFunc( GL_ALWAYS, 1, 255 );

	GL_UseProgram( depthProgram );

	const viewEntity_t* currentSpace = nullptr;
	bool depthHackActive = false;
	bool alphatestEnabled = false;
	idScreenRect currentScissor;
	if (r_useScissor.GetBool()) {
		glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		currentScissor.x1 = 0;
		currentScissor.y1 = 0;
		currentScissor.x2 = glConfig.vidWidth;
		currentScissor.y2 = glConfig.vidHeight;
	}

	fhRenderProgram::SetProjectionMatrix( backEnd.viewDef->projectionMatrix );
	fhRenderProgram::SetAlphaTestEnabled( false );

	const int num = renderlist.Num();
	for (int i = 0; i < num; ++i) {
		const auto& drawdepth = renderlist[i];

		const auto offset = vertexCache.Bind( drawdepth.surf->geo->ambientCache );
		GL_SetupVertexAttributes( fhVertexLayout::Draw, offset );

		if (currentSpace != drawdepth.surf->space) {
			fhRenderProgram::SetModelMatrix( drawdepth.surf->space->modelMatrix );
			fhRenderProgram::SetModelViewMatrix( drawdepth.surf->space->modelViewMatrix );

			if (drawdepth.surf->space->modelDepthHack) {
				RB_EnterModelDepthHack( drawdepth.surf->space->modelDepthHack );
				fhRenderProgram::SetProjectionMatrix( GL_ProjectionMatrix.Top() );
				depthHackActive = true;
			}
			else if (drawdepth.surf->space->weaponDepthHack) {
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
			if (r_useScissor.GetBool() && !currentScissor.Equals( drawdepth.surf->scissorRect )) {
				currentScissor = drawdepth.surf->scissorRect;
				glScissor( backEnd.viewDef->viewport.x1 + currentScissor.x1,
					backEnd.viewDef->viewport.y1 + currentScissor.y1,
					currentScissor.x2 + 1 - currentScissor.x1,
					currentScissor.y2 + 1 - currentScissor.y1 );
			}

			currentSpace = drawdepth.surf->space;
		}

		if (drawdepth.polygonOffset) {
			glEnable( GL_POLYGON_OFFSET_FILL );
			glPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() * drawdepth.polygonOffset );
		}

		if (drawdepth.isSubView) {
			GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO | GLS_DEPTHFUNC_LESS );
		}

		if (drawdepth.texture) {
			fhRenderProgram::SetAlphaTestEnabled( true );
			fhRenderProgram::SetAlphaTestThreshold( drawdepth.alphaTestThreshold );
			fhRenderProgram::SetDiffuseMatrix( drawdepth.textureMatrix[0], drawdepth.textureMatrix[1] );
			drawdepth.texture->Bind( 0 );
			alphatestEnabled = true;
		}
		else {
			if (alphatestEnabled) {
				globalImages->whiteImage->Bind( 0 );
				fhRenderProgram::SetAlphaTestEnabled( false );
				alphatestEnabled = false;;
			}
		}

		fhRenderProgram::SetDiffuseColor(drawdepth.color);

		backEnd.stats.groups[backEndGroup::DepthPrepass].drawcalls += 1;
		backEnd.stats.groups[backEndGroup::DepthPrepass].tris += drawdepth.surf->geo->numIndexes / 3;
		RB_DrawElementsWithCounters( drawdepth.surf->geo );

		if (drawdepth.polygonOffset) {
			glDisable( GL_POLYGON_OFFSET_FILL );
		}

		if (drawdepth.isSubView) {
			GL_State( GLS_DEPTHFUNC_LESS );
		}
	}

	if (r_useScissor.GetBool()) {
		glScissor( 0, 0, glConfig.vidWidth, glConfig.vidHeight );
		backEnd.currentScissor.x1 = 0;
		backEnd.currentScissor.y1 = 0;
		backEnd.currentScissor.x2 = glConfig.vidWidth;
		backEnd.currentScissor.y2 = glConfig.vidHeight;
	}
}

static void RB_GLSL_CreateFillDepthRenderList( drawSurf_t **drawSurfs, int numDrawSurfs, DepthRenderList& renderlist ) {
	for (int i = 0; i < numDrawSurfs; i++) {
		drawDepth_t drawdepth;

		const drawSurf_t* surf = drawSurfs[i];
		const srfTriangles_t* tri = surf->geo;
		const idMaterial* shader = surf->material;

		if (!shader->IsDrawn()) {
			continue;
		}

		// some deforms may disable themselves by setting numIndexes = 0
		if (!tri->numIndexes) {
			continue;
		}

		// translucent surfaces don't put anything in the depth buffer and don't
		// test against it, which makes them fail the mirror clip plane operation
		if (shader->Coverage() == MC_TRANSLUCENT) {
			continue;
		}

		if (!tri->ambientCache) {
			common->Printf( "RB_T_FillDepthBuffer: !tri->ambientCache\n" );
			continue;
		}

		// get the expressions for conditionals / color / texcoords
		const float	* regs = surf->shaderRegisters;

		// if all stages of a material have been conditioned off, don't do anything
		int stage = 0;
		for (; stage < shader->GetNumStages(); stage++) {
			const shaderStage_t* pStage = shader->GetStage( stage );
			// check the stage enable condition
			if (regs[pStage->conditionRegister] != 0) {
				break;
			}
		}
		if (stage == shader->GetNumStages()) {
			continue;
		}

		// set polygon offset if necessary
		if (shader->TestMaterialFlag( MF_POLYGONOFFSET )) {
			drawdepth.polygonOffset = shader->GetPolygonOffset();
		}
		else {
			drawdepth.polygonOffset = 0;
		}

		// subviews will just down-modulate the color buffer by overbright		
		if (shader->GetSort() == SS_SUBVIEW) {
			drawdepth.isSubView = true;
			drawdepth.color[0] = drawdepth.color[1] = drawdepth.color[2] = (1.0 / backEnd.overBright);
			drawdepth.color[3] = 1;
		}
		else {
			drawdepth.isSubView = false;
			drawdepth.color[0] = drawdepth.color[1] = drawdepth.color[2] = 0;
			drawdepth.color[3] = 1;
		}


		drawdepth.surf = surf;
		drawdepth.texture = nullptr;

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
				const shaderStage_t* pStage = shader->GetStage( stage );

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
				drawdepth.color[3] = regs[pStage->color.registers[3]];

				// skip the entire stage if alpha would be black
				if (drawdepth.color[3] <= 0) {
					continue;
				}

				// bind the texture
				drawdepth.texture = pStage->texture.image;
				drawdepth.alphaTestThreshold = regs[pStage->alphaTestRegister];

				// set texture matrix and texGens      

				if (pStage->privatePolygonOffset && !surf->material->TestMaterialFlag( MF_POLYGONOFFSET )) {
					drawdepth.polygonOffset = pStage->privatePolygonOffset;
				}

				if (pStage->texture.hasMatrix) {
					RB_GetShaderTextureMatrix( surf->shaderRegisters, &pStage->texture, drawdepth.textureMatrix );
				}
				else {
					drawdepth.textureMatrix[0] = idVec4::identityS;
					drawdepth.textureMatrix[1] = idVec4::identityT;
				}

				// Append drawdepth inside the loop, because some effects (eg burn-away of dead demons) 
				// require multiple stages being rendered to the depth buffer, because those stages are 
				// being blended/masked individually.
				renderlist.Append( drawdepth );
			}

			if (!didDraw) {
				drawSolid = true;
			}
		}

		if(drawSolid) {
			renderlist.Append( drawdepth );
		}
	}
}



void DepthRenderList::AddDrawSurfaces( drawSurf_t **surf, int numDrawSurfs ) {
	RB_GLSL_CreateFillDepthRenderList( surf, numDrawSurfs, *this);
}

void DepthRenderList::Submit() {
	RB_GLSL_SubmitFillDepthRenderList(*this);
}
