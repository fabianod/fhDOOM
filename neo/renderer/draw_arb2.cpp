/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_local.h"

typedef struct {
	GLenum			target;
	GLuint			ident;
	char			name[64];
} progDef_t;

static	const int	MAX_GLPROGS = 200;

// a single file can have both a vertex program and a fragment program
static progDef_t	progs[MAX_GLPROGS] = { {0} };

/*
====================
RB_ARB2_RenderCustomSpecialShaderStage
====================
*/
void RB_ARB2_RenderSpecialShaderStage( const float* regs, const shaderStage_t* pStage, newShaderStage_t* newStage, const srfTriangles_t	*tri ) {
	GL_UseProgram(nullptr);

	glEnable( GL_VERTEX_PROGRAM_ARB );
	glEnable( GL_FRAGMENT_PROGRAM_ARB );

	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, newStage->vertexProgram );
	glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, newStage->fragmentProgram );

	idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
	glVertexPointer( 3, GL_FLOAT, sizeof(idDrawVert), ac->xyz.ToFloatPtr() );
	glTexCoordPointer( 2, GL_FLOAT, sizeof(idDrawVert), reinterpret_cast<void *>(&ac->st) );
	glColorPointer( 4, GL_UNSIGNED_BYTE, sizeof(idDrawVert), (void *)&ac->color );
	glVertexAttribPointerARB( 9, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->tangents[0].ToFloatPtr() );
	glVertexAttribPointerARB( 10, 3, GL_FLOAT, false, sizeof(idDrawVert), ac->tangents[1].ToFloatPtr() );
	glNormalPointer( GL_FLOAT, sizeof(idDrawVert), ac->normal.ToFloatPtr() );

	glEnableClientState( GL_COLOR_ARRAY );
	glEnableVertexAttribArrayARB( 9 );
	glEnableVertexAttribArrayARB( 10 );
	glEnableClientState( GL_NORMAL_ARRAY );

	GL_State( pStage->drawStateBits );

	glMatrixMode( GL_PROJECTION );
	glLoadMatrixf( GL_ProjectionMatrix.Top() );
	glMatrixMode( GL_MODELVIEW );
	glLoadMatrixf( GL_ModelViewMatrix.Top() );

#if 0
	// megaTextures bind a lot of images and set a lot of parameters
	if (newStage->megaTexture) {
		newStage->megaTexture->SetMappingForSurface( tri );
		idVec3	localViewer;
		R_GlobalPointToLocal( surf->space->modelMatrix, backEnd.viewDef->renderView.vieworg, localViewer );
		newStage->megaTexture->BindForViewOrigin( localViewer );
	}
#endif

	for (int i = 0; i < newStage->numVertexParms; i++) {
		float	parm[4];
		parm[0] = regs[newStage->vertexParms[i][0]];
		parm[1] = regs[newStage->vertexParms[i][1]];
		parm[2] = regs[newStage->vertexParms[i][2]];
		parm[3] = regs[newStage->vertexParms[i][3]];
		glProgramLocalParameter4fvARB( GL_VERTEX_PROGRAM_ARB, i, parm );
	}

	for (int i = 0; i < newStage->numFragmentProgramImages; i++) {
		if (newStage->fragmentProgramImages[i]) {
			//GL_SelectTexture( i );
			newStage->fragmentProgramImages[i]->Bind(i);
		}
	}	

	// draw it
	RB_DrawElementsWithCounters( tri );

	for (int i = 1; i < newStage->numFragmentProgramImages; i++) {
		if (newStage->fragmentProgramImages[i]) {
			GL_SelectTexture( i );
			globalImages->BindNull();
		}
	}
	if (newStage->megaTexture) {
		newStage->megaTexture->Unbind();
	}

	GL_SelectTexture( 0 );

	glBindProgramARB( GL_VERTEX_PROGRAM_ARB, 0 );
	glBindProgramARB( GL_FRAGMENT_PROGRAM_ARB, 0 );

	glDisableClientState( GL_COLOR_ARRAY );
	glDisableVertexAttribArrayARB( 9 );
	glDisableVertexAttribArrayARB( 10 );
	glDisableClientState( GL_NORMAL_ARRAY );

	glDisable( GL_VERTEX_PROGRAM_ARB );
	glDisable( GL_FRAGMENT_PROGRAM_ARB );
}

/*
=================
R_LoadARBProgram
=================
*/
void R_LoadARBProgram( int progIndex ) {
	int		ofs;
	int		err;
	idStr	fullPath = "glprogs/";
	fullPath += progs[progIndex].name;
	char	*fileBuffer;
	char	*buffer;
	char	*start, *end;

	common->Printf( "%s", fullPath.c_str() );

	// load the program even if we don't support it, so
	// fs_copyfiles can generate cross-platform data dumps
	fileSystem->ReadFile( fullPath.c_str(), (void **)&fileBuffer, NULL );
	if ( !fileBuffer ) {
		common->Printf( ": File not found\n" );
		return;
	}

	// copy to stack memory and free
	buffer = (char *)_alloca( strlen( fileBuffer ) + 1 );
	strcpy( buffer, fileBuffer );
	fileSystem->FreeFile( fileBuffer );

	if ( !glConfig.isInitialized ) {
		return;
	}

	//
	// submit the program string at start to GL
	//
	if ( progs[progIndex].ident == 0 ) {
		// allocate a new identifier for this program
		progs[progIndex].ident = PROG_USER + progIndex;
	}

	// vertex and fragment programs can both be present in a single file, so
	// scan for the proper header to be the start point, and stamp a 0 in after the end

	if ( progs[progIndex].target == GL_VERTEX_PROGRAM_ARB ) {
		start = strstr( (char *)buffer, "!!ARBvp" );
	}
	if ( progs[progIndex].target == GL_FRAGMENT_PROGRAM_ARB ) {
		start = strstr( (char *)buffer, "!!ARBfp" );
	}
	if ( !start ) {
		common->Printf( ": !!ARB not found\n" );
		return;
	}
	end = strstr( start, "END" );

	if ( !end ) {
		common->Printf( ": END not found\n" );
		return;
	}
	end[3] = 0;

	glBindProgramARB( progs[progIndex].target, progs[progIndex].ident );
	glGetError();

	glProgramStringARB( progs[progIndex].target, GL_PROGRAM_FORMAT_ASCII_ARB,
		strlen( start ), (unsigned char *)start );

	err = glGetError();
	glGetIntegerv( GL_PROGRAM_ERROR_POSITION_ARB, (GLint *)&ofs );
	if ( err == GL_INVALID_OPERATION ) {
		const GLubyte *str = glGetString( GL_PROGRAM_ERROR_STRING_ARB );
		common->Printf( "\nGL_PROGRAM_ERROR_STRING_ARB: %s\n", str );
		if ( ofs < 0 ) {
			common->Printf( "GL_PROGRAM_ERROR_POSITION_ARB < 0 with error\n" );
		} else if ( ofs >= (int)strlen( (char *)start ) ) {
			common->Printf( "error at end of program\n" );
		} else {
			common->Printf( "error at %i:\n%s", ofs, start + ofs );
		}
		return;
	}
	if ( ofs != -1 ) {
		common->Printf( "\nGL_PROGRAM_ERROR_POSITION_ARB != -1 without error\n" );
		return;
	}

	common->Printf( "\n" );
}

/*
==================
R_FindARBProgram

Returns a GL identifier that can be bound to the given target, parsing
a text file if it hasn't already been loaded.
==================
*/
int R_FindARBProgram( GLenum target, const char *program ) {
	int		i;
	idStr	stripped = program;

	stripped.StripFileExtension();

	// see if it is already loaded
	for ( i = 0 ; progs[i].name[0] ; i++ ) {
		if ( progs[i].target != target ) {
			continue;
		}

		idStr	compare = progs[i].name;
		compare.StripFileExtension();

		if ( !idStr::Icmp( stripped.c_str(), compare.c_str() ) ) {
			return progs[i].ident;
		}
	}

	if ( i == MAX_GLPROGS ) {
		common->Error( "R_FindARBProgram: MAX_GLPROGS" );
	}

	// add it to the list and load it
	progs[i].ident = (program_t)0;	// will be gen'd by R_LoadARBProgram
	progs[i].target = target;
	strncpy( progs[i].name, program, sizeof( progs[i].name ) - 1 );

	R_LoadARBProgram( i );

	return progs[i].ident;
}

/*
==================
R_ReloadARBPrograms_f
==================
*/
void R_ReloadARBPrograms_f( const idCmdArgs &args ) {
	int		i;

	common->Printf( "----- R_ReloadARBPrograms -----\n" );
	for ( i = 0 ; progs[i].name[0] ; i++ ) {
		R_LoadARBProgram( i );
	}
	common->Printf( "-------------------------------\n" );
}