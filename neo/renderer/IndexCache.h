#pragma once

typedef struct indexBuffer_s {
	GLuint name;
	int size;
	struct indexBuffer_s* next;
	struct indexCache_s* first;
} indexBuffer_t;

typedef struct indexCache_s {
	indexBuffer_t* buffer;
	int offset;
	int size;
	bool used;

	struct indexCache_s* nextFree;
	struct indexCache_s* prevFree;
	struct indexCache_s* nextInBuffer;
	struct indexCache_s* prevInBuffer;
} indexCache_t;

class fhIndexCache {
public:
							fhIndexCache();
							~fhIndexCache();
	const indexCache_t*		Alloc( void *data, int bytes );	
	void                    Bind(const indexCache_t* cache);
	void					Unbind();	
	const indexCache_t*		AllocFrameTemp( void *data, int bytes );	
	void					Free( const indexCache_t* buffer );  	
	void					EndFrame();

private:
	indexCache_t* FindSmallestFree(int minSize);
	indexCache_t* CreateBuffer(int minSize);
	void DetachFromFreeList(indexCache_t* cache);
	void DetachFromBufferList(indexCache_t* cache);
	void PrependToFreeList(indexCache_t* cache);
	void BindBuffer(GLuint name);

	GLuint currentBuffer;

	indexBuffer_t* buffers;
	indexCache_t*  firstFree;

	idBlockAlloc<indexCache_t,1024>	cacheHeaderAllocator;
	idBlockAlloc<indexBuffer_t,32>	bufferHeaderAllocator;
};

extern fhIndexCache indexCache;
