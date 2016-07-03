#version 330
#extension GL_ARB_shading_language_420pack: enable

uniform mat4 rpModelMatrix;
uniform mat4 rpViewMatrix;
uniform mat4 rpModelViewMatrix;
uniform mat4 rpProjectionMatrix;

uniform vec4 rpLocalLightOrigin;
uniform vec4 rpLocalViewOrigin;
uniform vec4 rpLightProjectionS;
uniform vec4 rpLightProjectionT;
uniform vec4 rpLightProjectionQ;
uniform vec4 rpLightFallOff;

uniform vec4 rpBumpMatrixS;
uniform vec4 rpBumpMatrixT;
uniform vec4 rpDiffuseMatrixS;
uniform vec4 rpDiffuseMatrixT;
uniform vec4 rpSpecularMatrixS;
uniform vec4 rpSpecularMatrixT;

uniform vec4 rpColorModulate;
uniform vec4 rpColorAdd;
uniform vec4 rpDiffuseColor;
uniform vec4 rpSpecularColor;

uniform vec4 shaderParm0;
uniform vec4 shaderParm1;
uniform vec4 shaderParm2;
uniform vec4 shaderParm3;

uniform mat4 textureMatrix0;

uniform int rpAlphaTestEnabled;
uniform float rpAlphaTestThreshold;
uniform vec4 rpCurrentRenderSize;

uniform vec2 rpClipRange;
uniform int rpDepthBlendMode;
uniform float rpDepthBlendRange;
uniform float rpPomMaxHeight;
uniform int rpShading;
uniform float rpSpecularExp;

uniform int rpShadowMappingMode;
uniform mat4 rpSpotlightProjection;
uniform mat4 rpPointlightProjection[6];

uniform vec4 rpGlobalLightOrigin;
uniform vec4 rpShadowParams; //x=minBias, y=maxbias, z=fuzzyness, w=samples
uniform vec4 rpShadowCoords[6];
uniform vec4 rpShadowMapSize[6];  //xy = width/height of far plane, zw = near/far clip distance
                                  //TODO(johl): is width/height correctly set for non-parallel lights? 
uniform float rpCascadeDistances[5];
uniform mat4 rpInverseLightRotation;