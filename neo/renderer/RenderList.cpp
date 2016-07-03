#include "tr_local.h"
#include "RenderList.h"
#include "RenderProgram.h"
#include <atomic>

static const uint32 renderlistMaxSize = 1024 * 1024 * 64;
static void* renderlistMemory = nullptr;
static std::atomic<uint32> allocated(0);

void fhBaseRenderList::Init() {
	assert(!renderlistMemory);
	renderlistMemory = R_StaticAlloc(renderlistMaxSize);
}

void fhBaseRenderList::EndFrame() {
	allocated = 0;
}

void* fhBaseRenderList::AllocateBytes(uint32 bytes) {
	uint32 offset = allocated.fetch_add(bytes);
	assert(offset + bytes < renderlistMaxSize);
	assert(renderlistMemory);

	return &static_cast<char*>(renderlistMemory)[offset];
}
