#pragma once
#include <type_traits>


class fhBaseRenderList {
public:
	static void Init();
	static void EndFrame();
protected:
	template<typename T>
	T* AllocateArray(uint32 num) {
		static_assert(std::is_trivial<T>::value, "T must be trivial");
		return static_cast<T*>(AllocateBytes(sizeof(T) * num));
	}

private:
	void* AllocateBytes(uint32 bytes);
};


template<typename T>
class fhRenderList : public fhBaseRenderList {
public:
	fhRenderList()
		: array(nullptr)
		, capacity(0)
		, size(0) {
	}

	void Append(const T& t) {
		if(size == capacity) {
			capacity += 1024;
			T* t = AllocateArray<T>(capacity);

			if (size > 0) {
				memcpy(t, array, sizeof(T) * size);
			}

			array = t;			
		}

		assert(size < capacity);
		array[size] = t;
		++size;
	}

	void Clear() {
		size = 0;
	}

	int Num() const {
		return size;
	}

	bool IsEmpty() const {
		return Num() == 0;
	}

	const T& operator[](int i) const {
		assert(i < size);
		return array[i];
	}

private:
	T*  array;
	int capacity;
	int size;
};

struct drawDepth_t {
	const drawSurf_t*  surf;
	idImage*           texture;	
	idVec4             textureMatrix[2];
	idVec4             color;
	float              polygonOffset;
	bool               isSubView;	
	float              alphaTestThreshold;
}; 

struct drawShadow_t {
	const srfTriangles_t* tris;
	const idRenderEntityLocal* entity;
	idImage*           texture;
	idVec4             textureMatrix[2];
	bool               hasTextureMatrix;
	float              alphaTestThreshold;
	unsigned           visibleFlags;
};

struct drawStage_t {
	const drawSurf_t* surf;
	const fhRenderProgram*  program;
	idImage*          textures[4];
	idCinematic*      cinematic;
	float             textureMatrix[16];
	bool              hasBumpMatrix;
	idVec4            bumpMatrix[2];
	depthBlendMode_t  depthBlendMode;
	float             depthBlendRange;
	stageVertexColor_t vertexColor;
	float             polygonOffset;
	idVec4            localViewOrigin;
	idVec4            diffuseColor;
	cullType_t        cullType;
	int               drawStateBits;
	idVec4            shaderparms[4];
	int               numShaderparms;
	fhVertexLayout    vertexLayout;
};

static_assert(std::is_trivial<drawDepth_t>::value, "must be trivial");
static_assert(std::is_trivial<drawShadow_t>::value, "must be trivial");
static_assert(std::is_trivial<drawStage_t>::value, "must be trivial");
static_assert(std::is_trivial<drawInteraction_t>::value, "must be trivial");

class DepthRenderList : public fhRenderList<drawDepth_t> {
public:
	void AddDrawSurfaces( drawSurf_t **surf, int numDrawSurfs );
	void Submit();
};

class StageRenderList : public fhRenderList<drawStage_t> {
public:
	void AddDrawSurfaces( drawSurf_t **surf, int numDrawSurfs );
	void Submit();
};

class InteractionList : public fhRenderList<drawInteraction_t> {
public:
	void AddDrawSurfacesOnLight(const drawSurf_t *surf);
	void Submit();
};

class ShadowRenderList : public fhRenderList<drawShadow_t> {
public:
	ShadowRenderList();
	void AddInteractions( viewLight_t* vlight, const shadowMapFrustum_t* shadowFrustrums, int numShadowFrustrums );
	void Submit( const float* shadowViewMatrix, const float* shadowProjectionMatrix, int side, int lod ) const;
private:
	void AddSurfaceInteraction( const idRenderEntityLocal *entityDef, const srfTriangles_t *tri, const idMaterial* material, unsigned visibleSides );
	idRenderEntityLocal dummy;
};

int  RB_GLSL_CreateStageRenderList( drawSurf_t **drawSurfs, int numDrawSurfs, StageRenderList& renderlist, int maxSort );
void RB_GLSL_SubmitStageRenderList( const StageRenderList& renderlist );