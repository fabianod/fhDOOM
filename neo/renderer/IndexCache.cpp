#include "../idlib/precompiled.h"
#pragma hdrstop

#include "tr_local.h"
#include "IndexCache.h"

fhIndexCache indexCache;

fhIndexCache::fhIndexCache()
: currentBuffer(0)
, buffers(nullptr)
, firstFree(nullptr) {
}

fhIndexCache::~fhIndexCache() {
}

const indexCache_t*  fhIndexCache::Alloc( void *data, int bytes ) {

	indexCache_t* ret = FindSmallestFree(bytes);

	if (!ret) {
		ret = CreateBuffer(bytes);
	}

	assert(ret);

	if (ret->size > bytes) {
		indexCache_t* remaining = cacheHeaderAllocator.Alloc();
		remaining->buffer = ret->buffer;
		remaining->offset = ret->offset + bytes;
		remaining->size = ret->size - bytes;
		remaining->used = false;
		remaining->prevFree = nullptr;
		remaining->nextFree = nullptr;

		if(ret->nextInBuffer)
			ret->nextInBuffer->prevInBuffer = remaining;
		remaining->nextInBuffer = ret->nextInBuffer;
		ret->nextInBuffer = remaining;
		remaining->prevInBuffer = ret;

		PrependToFreeList(remaining);

		ret->size = bytes;
	}
	
	DetachFromFreeList(ret);
	ret->used = true;
	Bind(ret);

	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, ret->offset, bytes, data);
	
	return ret;
}

void fhIndexCache::Bind( const indexCache_t* cache ) {
	assert(cache);
	assert(cache->buffer);
	assert(cache->buffer->name);

	BindBuffer(cache->buffer->name);
}

void fhIndexCache::Unbind() {
	BindBuffer(0);
}

const indexCache_t* fhIndexCache::AllocFrameTemp( void *data, int bytes ) {
	return nullptr;
}

void fhIndexCache::Free( const indexCache_t* buffer ) {
	if(!buffer) {
		return;
	}

	assert(buffer->used);

	indexCache_t* b = const_cast<indexCache_t*>(buffer);

	if(b->nextInBuffer && !b->nextInBuffer->used && b->nextInBuffer->offset == b->offset + b->size) {
		indexCache_t* next = b->nextInBuffer;
		DetachFromFreeList(next);
		DetachFromBufferList(next);		
		b->size += next->size;
		cacheHeaderAllocator.Free(next);
	}

	if (b->prevInBuffer && !b->prevInBuffer->used && b->prevInBuffer->offset + b->prevInBuffer->size == b->offset) {
		indexCache_t* prev = b->prevInBuffer;
		DetachFromFreeList( prev );
		DetachFromBufferList( prev );
		b->size += prev->size;
		b->offset = prev->offset;
		cacheHeaderAllocator.Free(prev);
	}

	b->used = false;
	PrependToFreeList(b);
}

void fhIndexCache::EndFrame() {
}


indexCache_t* fhIndexCache::FindSmallestFree( int minSize ) {
	indexCache_t* ret = nullptr;

	for (indexCache_t* c = firstFree; c != nullptr; c = c->nextFree) {
		if (c->size < minSize)
			continue;

		if (ret && ret->size < c->size)
			continue;

		ret = c;
	}

	return ret;
}

indexCache_t* fhIndexCache::CreateBuffer( int minSize ) {
	indexBuffer_t* buffer = bufferHeaderAllocator.Alloc();
	buffer->size = max( 1024 * 1024 * 4, minSize );

	glGenBuffers( 1, &buffer->name );
	assert( buffer->name != 0 );
	BindBuffer( buffer->name );
	glBufferData( GL_ELEMENT_ARRAY_BUFFER, buffer->size, nullptr, GL_STATIC_DRAW );

	buffer->next = buffers;
	buffers = buffer;

	indexCache_t* cache = cacheHeaderAllocator.Alloc();
	cache->used = false;
	cache->buffer = buffer;
	cache->offset = 0;
	cache->size = buffer->size;
	cache->nextInBuffer = nullptr;
	cache->prevInBuffer = nullptr;
	cache->prevFree = nullptr;
	cache->nextFree = firstFree;	

	if(cache->nextFree)
		cache->nextFree->prevFree = cache;

	firstFree = cache;
	buffer->first = cache;

	return cache;
}

void fhIndexCache::DetachFromFreeList(indexCache_t* cache) {
	if (cache->nextFree) {
		cache->nextFree->prevFree = cache->prevFree;
	}

	if (cache->prevFree) {
		cache->prevFree->nextFree = cache->nextFree;
	} else {
		assert(cache == firstFree);
		firstFree = cache->nextFree;
	}

	cache->nextFree = nullptr;
	cache->prevFree = nullptr;
}

void fhIndexCache::DetachFromBufferList(indexCache_t* cache) {
	if (cache->nextInBuffer) {
		cache->nextInBuffer->prevInBuffer = cache->prevInBuffer;
	}

	if (cache->prevInBuffer) {
		cache->prevInBuffer->nextInBuffer = cache->nextInBuffer;
	} else {
		assert(cache->buffer);
		assert(cache->buffer->first == cache);
		cache->buffer->first = cache->nextInBuffer;
	}

	cache->nextInBuffer = nullptr;
	cache->prevInBuffer = nullptr;
}

void fhIndexCache::PrependToFreeList(indexCache_t* cache) {
	assert(cache);
	assert(!cache->used);
	assert(!cache->prevFree);
	assert(!cache->nextFree);

	if(firstFree)
		firstFree->prevFree = cache;

	cache->nextFree = firstFree;
	firstFree = cache;
}

void fhIndexCache::BindBuffer(GLuint name) {
	if(currentBuffer != name) {
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, name);
		currentBuffer = name;
	}
}
