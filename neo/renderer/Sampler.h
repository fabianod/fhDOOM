#pragma once

#include "Material.h"

class fhSampler {
public:
	fhSampler();
	~fhSampler();

	void Bind(int textureUnit);
	void Purge();

	void SetParams(textureFilter_t filter, textureRepeat_t repeat, bool useAf = true, bool useLodBias = true);

	static fhSampler* GetSampler(textureFilter_t filter, textureRepeat_t repeat, bool useAf = true, bool useLodBias = true);
	static fhSampler* CreateSampler(textureFilter_t filter, textureRepeat_t repeat, bool useAf = true, bool useLodBias = true);
private:
	void Init();

	GLuint          num;
	textureFilter_t filter;
	textureRepeat_t repeat;
	bool            useAf;
	bool            useLodBias;
};