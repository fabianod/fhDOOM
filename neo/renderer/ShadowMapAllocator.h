#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_local.h"

#pragma once

class fhShadowMapAllocator {
public:
	fhShadowMapAllocator();
	~fhShadowMapAllocator();

	bool Allocate( int lod, int num, shadowCoord_t* coords );
	void FreeAll();

private:
	enum class ShadowMapSize
	{
		SM4096,
		SM2048,
		SM1024,
		SM512,
		SM256,
		NUM
	};

	bool Allocate( ShadowMapSize size, int num, shadowCoord_t* coords );
	bool Allocate( ShadowMapSize size, shadowCoord_t& coords );
	bool Make( int sizeIndex );
	void Split( const shadowCoord_t& src, shadowCoord_t* dst, idVec2 scale, float size );

	idList<shadowCoord_t> freelist[(int)ShadowMapSize::NUM];
};

extern fhShadowMapAllocator shadowMapAllocator;