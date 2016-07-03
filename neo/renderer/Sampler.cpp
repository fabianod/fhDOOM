#include "Image.h"
#include "Sampler.h"
#include "tr_local.h"

static const int maxSamplerSettings = 128;
static fhSampler samplers[maxSamplerSettings];


fhSampler::fhSampler()
	: num(0)
	, filter(TF_DEFAULT)
	, repeat(TR_REPEAT)
	, useAf(true)
	, useLodBias(true) {
}

fhSampler::~fhSampler() {
}

void fhSampler::Bind( int textureUnit ) {
	if(num == 0) {
		Init();
	}

	if(backEnd.glState.tmu[textureUnit].currentSampler != num) {
		glBindSampler(textureUnit, num);
		backEnd.glState.tmu[textureUnit].currentSampler = num;
	}
}

fhSampler* fhSampler::GetSampler( textureFilter_t filter, textureRepeat_t repeat, bool useAf, bool useLodBias ) {

	int i = 0;
	for(; i < maxSamplerSettings; ++i) {
		const fhSampler& s = samplers[i];

		if(s.num == 0)
			break;

		if(s.filter != filter)
			continue;

		if (s.repeat != repeat)
			continue;

		if (s.useAf != useAf)
			continue;

		if (s.useLodBias != useLodBias)
			continue;

		return &samplers[i];
	}

	if(i == maxSamplerSettings)
		return nullptr;

	fhSampler& sampler = samplers[i];
	sampler.filter = filter;
	sampler.repeat = repeat;
	sampler.useAf = useAf;
	sampler.useLodBias = useLodBias;

	sampler.Init();

	return &sampler;
}

fhSampler* fhSampler::CreateSampler(textureFilter_t filter, textureRepeat_t repeat, bool useAf /* = true */, bool useLodBias /* = true */) {
	fhSampler* sampler = new fhSampler;
	sampler->filter = filter;
	sampler->repeat = repeat;
	sampler->useAf = useAf;
	sampler->useLodBias = useLodBias;
	sampler->Init();
	return sampler;
}

void fhSampler::Purge() {
	if(num > 0)
		glDeleteSamplers(1, &num);

	num = 0;
}

void fhSampler::Init() {
	if(num <= 0) {
		glGenSamplers( 1, &num );
	}

	switch (filter) {
	case TF_DEFAULT:
		glSamplerParameteri( num, GL_TEXTURE_MIN_FILTER, globalImages->textureMinFilter );
		glSamplerParameteri( num, GL_TEXTURE_MAG_FILTER, globalImages->textureMaxFilter );
		break;
	case TF_LINEAR:
		glSamplerParameteri( num, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glSamplerParameteri( num, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		break;
	case TF_NEAREST:
		glSamplerParameteri( num, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glSamplerParameteri( num, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		break;
	default:
		common->FatalError( "fhSampler: bad texture filter" );
	}

	switch (repeat) {
	case TR_REPEAT:
		glSamplerParameteri( num, GL_TEXTURE_WRAP_S, GL_REPEAT );
		glSamplerParameteri( num, GL_TEXTURE_WRAP_T, GL_REPEAT );
		break;
	case TR_CLAMP_TO_BORDER:
		glSamplerParameteri( num, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER );
		glSamplerParameteri( num, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER );
		break;
	case TR_CLAMP_TO_ZERO:
	case TR_CLAMP_TO_ZERO_ALPHA:
	case TR_CLAMP:
		glSamplerParameteri( num, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glSamplerParameteri( num, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		break;
	default:
		common->FatalError( "fhSampler: bad texture repeat" );
	}
	
	

	if (glConfig.anisotropicAvailable) {
		// only do aniso filtering on mip mapped images
		if (filter == TF_DEFAULT && useAf) {
			glSamplerParameterf( num, GL_TEXTURE_MAX_ANISOTROPY_EXT, globalImages->textureAnisotropy );
		}
		else {
			glSamplerParameterf( num, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1 );;
		}
	}

	if(useLodBias) {
		glSamplerParameterf( num, GL_TEXTURE_LOD_BIAS, globalImages->textureLODBias );
	} else {
		glSamplerParameterf( num, GL_TEXTURE_LOD_BIAS, 0 );
	}
}

void fhSampler::SetParams(textureFilter_t filter, textureRepeat_t repeat, bool useAf, bool useLodBias) {
	this->filter = filter;
	this->repeat = repeat;
	this->useAf = useAf;
	this->useLodBias = useLodBias;
	Purge();
	Init();
}