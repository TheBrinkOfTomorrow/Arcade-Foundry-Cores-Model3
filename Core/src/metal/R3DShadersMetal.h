// Supermodel's New3D shaders (R3DShaderQuads.h, R3DShaderCommon.h,
// R3DScrollFog.cpp, R3DFrameBuffers.cpp) and its 2D layer shader
// (Shaders2D.h) in the Metal Shading Language, for R3DGpuMetal and
// Render2DMetal. The GLSL is followed line by line; where Metal differs:
//
// - The geometry shader is gone: each quad is drawn as 6 vertices (two
//   triangles, the ones the GLSL's triangle strip makes), and every one of
//   them loads all 4 of the quad's vertices from the vertex buffer and
//   computes what the geometry shader did for that corner.
// - Positions leave the vertex stage with y negated and z = (z + w) / 2, so
//   the pixels land in memory as OpenGL's do (row 0 at the bottom of the
//   picture, gl_FragCoord equal to Metal's [[position]]) and clipping is
//   OpenGL's -w..w.
// - GLSL mod() is floor based; Metal's fmod() is not, so GlslMod().
// - Compile without fast math: the quad interpolation counts on exact
//   float behaviour (the GLSL marks it precise).

#pragma once

static const char* kR3DShadersMetal = R"msl(

#include <metal_stdlib>
using namespace metal;

// ---- shared with R3DGpuMetal.mm (layout must match R3DUniforms there) ----

struct R3DUniforms
{
	float4x4	projMat;
	float4x4	modelMat;
	float4		fogColour;			// rgb
	float4		lighting0;			// sun direction
	float4		lighting1;			// x = diffuse, y = ambient
	float4		spotEllipse;
	float4		spotColor;			// rgb
	float4		spotFogColor;		// rgb
	float4		spotRange;			// xy
	float		fogIntensity;
	float		fogDensity;
	float		fogStart;
	float		fogAttenuation;
	float		fogAmbient;
	float		cota;
	float		modelScale;
	float		nodeAlpha;
	float		microTextureMinLOD;
	float		shininess;
	float		specularValue;
	float		layerDepthBias;
	int4		baseTexInfo;
	int2		textureWrapMode;
	int			baseTexType;
	int			texturePage;
	int			microTextureID;
	int			colourLayer;
	uint		flags;
	int			pad;
};

#define F_TEXTURE_ENABLED	(1u << 0)
#define F_MICRO_TEXTURE		(1u << 1)
#define F_TEXTURE_INVERTED	(1u << 2)
#define F_TEXTURE_ALPHA		(1u << 3)
#define F_ALPHA_TEST		(1u << 4)
#define F_DISCARD_ALPHA		(1u << 5)
#define F_LIGHT_ENABLED		(1u << 6)
#define F_SUN_CLAMP			(1u << 7)
#define F_INTENSITY_CLAMP	(1u << 8)
#define F_SPECULAR_ENABLED	(1u << 9)
#define F_FIXED_SHADING		(1u << 10)
#define F_SMOOTH_SHADING	(1u << 11)
#define F_STEP15_LUMINOUS	(1u << 12)
#define F_TRANSLATOR_MAP	(1u << 13)

struct FVertex					// New3D::FVertex, 60 bytes
{
	packed_float4	pos;
	packed_float3	normal;
	packed_float2	texcoords;
	float			fixedShade;
	packed_float3	faceNormal;
	uchar4			faceColour;
	float			textureNP;
};

static float4 GlPositionToMetal(float4 p)
{
	return float4(p.x, -p.y, (p.z + p.w) * 0.5, p.w);
}

static float GlslMod(float x, float y)
{
	return x - y * floor(x / y);
}

// ---- vertex shader (vertexShaderR3DQuads) ----

struct VsOut
{
	float3	viewVertex;
	float3	viewNormal;
	float2	texCoord;
	float4	color;
	float	fixedShade;
	float	discardPoly;
	float	LODBase;
	float4	position;
};

static float3x3 Mat3(float4x4 m)
{
	return float3x3(m[0].xyz, m[1].xyz, m[2].xyz);
}

static VsOut R3DVertex(FVertex v, constant R3DUniforms& u)
{
	float4 inVertex		= float4(v.pos);
	float3 inNormal		= float3(v.normal);
	float3 inFaceNormal	= float3(v.faceNormal);
	float4 inColour		= float4(v.faceColour) / 255.0;

	VsOut o;
	o.viewVertex	= (u.modelMat * inVertex).xyz;
	o.viewNormal	= (Mat3(u.modelMat) / u.modelScale) * inNormal;

	// CalcBackFace
	float3 vn		= Mat3(u.modelMat) * inFaceNormal;
	o.discardPoly	= dot(o.viewVertex, vn);

	// GetColour
	float4 c = inColour;
	if ((u.flags & F_TRANSLATOR_MAP) != 0) {
		c.rgb *= 16.0;
	}
	c.a *= u.nodeAlpha;
	o.color			= c;

	o.texCoord		= float2(v.texcoords);
	o.fixedShade	= v.fixedShade;
	o.LODBase		= o.discardPoly * -u.cota * v.textureNP;
	o.position		= (u.projMat * u.modelMat) * inVertex;
	return o;
}

// ---- geometry shader (geometryShaderR3DQuads), per output corner ----

struct QuadOut
{
	float4	position			[[position]];
	float4	v01					[[center_no_perspective]];	// v[0].xy, v[1].xy
	float4	v23					[[center_no_perspective]];	// v[2].xy, v[3].xy
	float4	area				[[center_no_perspective]];
	float4	oneOverW			[[flat]];
	float3	viewVertex0			[[flat]];
	float3	viewVertex1			[[flat]];
	float3	viewVertex2			[[flat]];
	float3	viewVertex3			[[flat]];
	float3	viewNormal0			[[flat]];
	float3	viewNormal1			[[flat]];
	float3	viewNormal2			[[flat]];
	float3	viewNormal3			[[flat]];
	float4	texCoord01			[[flat]];
	float4	texCoord23			[[flat]];
	float4	color				[[flat]];
	float4	fixedShade			[[flat]];
	float	LODBase				[[flat]];
	float4	barycentricCoords;
};

//a*b - c*d, computed in a stable fashion (Kahan)
static float DifferenceOfProducts(float a, float b, float c, float d)
{
	float cd = c * d;
	float err = fma(-c, d, cd);
	float dop = fma(a, b, -cd);
	return dop + err;
}

vertex QuadOut r3dQuadVertex(uint vid [[vertex_id]],
                             const device FVertex* verts [[buffer(0)]],
                             constant R3DUniforms& u [[buffer(1)]])
{
	// the two triangles of the GLSL's strip (vertex order 1, 0, 2, 3)
	const uint corners[6] = { 1, 0, 2, 0, 2, 3 };
	uint quad	= vid / 6;
	int ii		= int(corners[vid % 6]);

	VsOut gs_in[4];
	for (int i = 0; i < 4; i++) {
		gs_in[i] = R3DVertex(verts[quad * 4 + i], u);
	}

	QuadOut o;

	if (gs_in[0].discardPoly > 0.0) {
		o.position = float4(2.0, 2.0, 2.0, 1.0);	// outside the clip volume: emulate back face culling
		return o;
	}

	bool triangle = all(gs_in[3].position == gs_in[2].position);

	o.color		= gs_in[0].color;
	o.LODBase	= gs_in[0].LODBase;

	float2 v[4];
	float oneOverW[4];
	for (int i = 0; i < 4; i++) {
		oneOverW[i]	= 1.0 / gs_in[i].position.w;
		v[i]		= gs_in[i].position.xy * oneOverW[i];
	}
	o.oneOverW		= float4(oneOverW[0], oneOverW[1], oneOverW[2], oneOverW[3]);
	o.viewVertex0	= gs_in[0].viewVertex * oneOverW[0];
	o.viewVertex1	= gs_in[1].viewVertex * oneOverW[1];
	o.viewVertex2	= gs_in[2].viewVertex * oneOverW[2];
	o.viewVertex3	= gs_in[3].viewVertex * oneOverW[3];
	o.viewNormal0	= gs_in[0].viewNormal * oneOverW[0];
	o.viewNormal1	= gs_in[1].viewNormal * oneOverW[1];
	o.viewNormal2	= gs_in[2].viewNormal * oneOverW[2];
	o.viewNormal3	= gs_in[3].viewNormal * oneOverW[3];
	o.texCoord01	= float4(gs_in[0].texCoord * oneOverW[0], gs_in[1].texCoord * oneOverW[1]);
	o.texCoord23	= float4(gs_in[2].texCoord * oneOverW[2], gs_in[3].texCoord * oneOverW[3]);
	o.fixedShade	= float4(gs_in[0].fixedShade * oneOverW[0], gs_in[1].fixedShade * oneOverW[1],
	                         gs_in[2].fixedShade * oneOverW[2], gs_in[3].fixedShade * oneOverW[3]);

	if (triangle) {
		// only vertices 0-2 (the 1st triangle); the 2nd one has no area
		o.v01 = float4(0.0);
		o.v23 = float4(0.0);
		o.area = float4(0.0);
		o.barycentricCoords = float4(float(ii == 0), float(ii == 1), float(ii == 2), 1.0);
		o.position = GlPositionToMetal(gs_in[ii].position);
		return o;
	}

	float crossp[16];
	for (int i = 0; i < 4; i++)
	{
		crossp[i*4+i] = 0.0;
		for (int j = i+1; j < 4; j++)
			crossp[i*4+j] = DifferenceOfProducts(gs_in[i].position.x, gs_in[j].position.y, gs_in[j].position.x, gs_in[i].position.y) / (gs_in[i].position.w * gs_in[j].position.w);
	}
	for (int i = 1; i < 4; i++)
		for (int j = 0; j < i; j++)
			crossp[i*4+j] = -crossp[j*4+i];

	float2 vd[4];
	float area[4];
	for (int j = 0; j < 4; j++) {
		vd[j] = v[j] - v[ii];
		int j_next = (j+1) % 4;
		area[j] = crossp[j*4+j_next] + crossp[j_next*4+ii] + crossp[ii*4+j];
	}
	o.v01	= float4(vd[0], vd[1]);
	o.v23	= float4(vd[2], vd[3]);
	o.area	= float4(area[0], area[1], area[2], area[3]);

	const float3 bary[4] = {
		float3(1.0, 0.0, 0.0),
		float3(0.0, 1.0, 0.0),
		float3(0.0, 0.0, 1.0),
		float3(0.0,-1.0, 0.0)		// make the last coordinate negative so we can tell the difference between the triangles
	};
	o.barycentricCoords = float4(bary[ii], 0.0);
	o.position = GlPositionToMetal(gs_in[ii].position);
	return o;
}

// ---- fragment shader (fragmentShaderR3DQuads + fragmentShaderR3DCommon) ----

#define LayerColour 0x0
#define LayerTrans0 0x1
#define LayerTrans1 0x2

struct FragState
{
	float3	fsViewVertex;
	float3	fsViewNormal;
	float2	fsTexCoord;
	float4	fsColor;
	float	fsFixedShade;
	float	fsLODBase;
	float4	fragCoord;
	bool	discarded;
};

struct R3DFragOut
{
	float4 out0 [[color(0)]];		// opaque
	float4 out1 [[color(1)]];		// trans layer 1
	float4 out2 [[color(2)]];		// trans layer 2
	float depth [[depth(any)]];
};

static float4 ExtractColour(int type, uint value)
{
	float4 c = float4(0.0);

	if(type==0) {			// T1RGB5
		c.r     = float((value >> 10) & 0x1Fu);
		c.g     = float((value >> 5 ) & 0x1Fu);
		c.b     = float((value      ) & 0x1Fu);
		c.rgb  *= (1.0/31.0);
		c.a     = 1.0 - float((value >> 15) & 0x1u);
	}
	else if(type==1) {		// Interleaved A4L4 (low byte)
		c.rgb   = float3(float(value&0xFu));
		c.a     = float((value >> 4) & 0xFu);
		c      *= (1.0/15.0);
	}
	else if(type==2) {
		c.a     = float(value&0xFu);
		c.rgb   = float3(float((value >> 4) & 0xFu));
		c      *= (1.0/15.0);
	}
	else if(type==3) {
		c.rgb   = float3(float((value>>8)&0xFu));
		c.a     = float((value >> 12) & 0xFu);
		c      *= (1.0/15.0);
	}
	else if(type==4) {
		c.a     = float((value>>8)&0xFu);
		c.rgb   = float3(float((value >> 12) & 0xFu));
		c      *= (1.0/15.0);
	}
	else if(type==5) {
		c = float4(float(value&0xFFu) / 255.0);
		if(c.a==1.0)	{ c.a = 0.0; }
		else			{ c.a = 1.0; }
	}
	else if(type==6) {
		c = float4(float((value>>8)&0xFFu) / 255.0);
		if(c.a==1.0)	{ c.a = 0.0; }
		else			{ c.a = 1.0; }
	}
	else if(type==7) {	// RGBA4
		c.r = float((value>>12)&0xFu);
		c.g = float((value>> 8)&0xFu);
		c.b = float((value>> 4)&0xFu);
		c.a = float((value>> 0)&0xFu);
		c *= (1.0/15.0);
	}
	else if(type==8) {	// low byte, low nibble
		c = float4(float(value&0xFu) / 15.0);
		if(c.a==1.0)	{ c.a = 0.0; }
		else			{ c.a = 1.0; }
	}
	else if(type==9) {	// low byte, high nibble
		c = float4(float((value>>4)&0xFu) / 15.0);
		if(c.a==1.0)	{ c.a = 0.0; }
		else			{ c.a = 1.0; }
	}
	else if(type==10) {	// high byte, low nibble
		c = float4(float((value>>8)&0xFu) / 15.0);
		if(c.a==1.0)	{ c.a = 0.0; }
		else			{ c.a = 1.0; }
	}
	else if(type==11) {	// high byte, high nibble
		c = float4(float((value>>12)&0xFu) / 15.0);
		if(c.a==1.0)	{ c.a = 0.0; }
		else			{ c.a = 1.0; }
	}

	return c;
}

static int2 WrapTexCoords(int2 coordinate, int level)
{
	int2 newCoord;
	newCoord.x = coordinate.x & (2047 >> level);
	newCoord.y = coordinate.y & (1023 >> level);

	return newCoord;
}

static int2 GetTextureSize(int level, int2 size)
{
	int mipDivisor = 1 << level;

	return size / mipDivisor;
}

static int2 GetTexturePosition(int level, int2 pos)
{
	int mipDivisor = 1 << level;

	int2 retPos;
	retPos.x = pos.x / mipDivisor;
	retPos.y = pos.y / mipDivisor;

	return retPos;
}

static int2 GetMicroTexturePos(int id)
{
	const int xCoords[8] = { 0, 0, 128, 128, 0, 0, 128, 128 };
	const int yCoords[8] = { 0, 128, 0, 128, 256, 384, 256, 384 };

	return int2(xCoords[id],yCoords[id]);
}

static float LinearTexLocations(int wrapMode, float size, float u, thread float& u0, thread float& u1)
{
	float texelSize		= 1.0 / size;
	float halfTexelSize	= 0.5 / size;

	if(wrapMode==0) {							// repeat
		u	= u * size - 0.5;
		u0	= (floor(u) + 0.5) / size;			// + 0.5 offset added to push us into the centre of a pixel, without we'll get rounding errors
		u0	= fract(u0);
		u1	= u0 + texelSize;
		u1	= fract(u1);

		return fract(u);						// return weight
	}
	else if(wrapMode==1) {						// repeat + clamp
		u	= fract(u);							// must force into 0-1 to start
		u	= u * size - 0.5;
		u0	= (floor(u) + 0.5) / size;			// + 0.5 offset added to push us into the centre of a pixel, without we'll get rounding errors
		u1	= u0 + texelSize;

		if(u0 <  0.0)	u0 = 0.0;
		if(u1 >= 1.0)	u1 = 1.0 - halfTexelSize;

		return fract(u);						// return weight
	}
	else {										// mirror + mirror clamp - both are the same since the edge pixels are repeated anyway
		float odd = floor(GlslMod(u, 2.0));		// odd values are mirrored

		if(odd > 0.0) {
			u = 1.0 - fract(u);
		}
		else {
			u = fract(u);
		}

		u	= u * size - 0.5;
		u0	= (floor(u) + 0.5) / size;			// + 0.5 offset added to push us into the centre of a pixel, without we'll get rounding errors
		u1	= u0 + texelSize;

		if(u0 <  0.0)	u0 = 0.0;
		if(u1 >= 1.0)	u1 = 1.0 - halfTexelSize;

		return fract(u);						// return weight
	}
}

static uint TexelFetch(texture2d<ushort, access::read> tex, int2 pos, int level)
{
	return uint(tex.read(uint2(pos), uint(level)).r);
}

static float4 texBiLinear(texture2d<ushort, access::read> texSampler, constant R3DUniforms& u, int2 wrapMode, float2 texSize, int2 texPos, float2 texCoord, int level)
{
	float tx[2], ty[2];
	float a = LinearTexLocations(wrapMode.x, texSize.x, texCoord.x, tx[0], tx[1]);
	float b = LinearTexLocations(wrapMode.y, texSize.y, texCoord.y, ty[0], ty[1]);

	float4 p0q0 = ExtractColour(u.baseTexType,TexelFetch(texSampler, WrapTexCoords(int2(float2(tx[0],ty[0]) * texSize + float2(texPos)),level), level));
	float4 p1q0 = ExtractColour(u.baseTexType,TexelFetch(texSampler, WrapTexCoords(int2(float2(tx[1],ty[0]) * texSize + float2(texPos)),level), level));
	float4 p0q1 = ExtractColour(u.baseTexType,TexelFetch(texSampler, WrapTexCoords(int2(float2(tx[0],ty[1]) * texSize + float2(texPos)),level), level));
	float4 p1q1 = ExtractColour(u.baseTexType,TexelFetch(texSampler, WrapTexCoords(int2(float2(tx[1],ty[1]) * texSize + float2(texPos)),level), level));

	if((u.flags & F_ALPHA_TEST) != 0) {
		if(p0q0.a > p1q0.a)		{ p1q0.rgb = p0q0.rgb; }
		if(p0q0.a > p0q1.a)		{ p0q1.rgb = p0q0.rgb; }

		if(p1q0.a > p0q0.a)		{ p0q0.rgb = p1q0.rgb; }
		if(p1q0.a > p1q1.a)		{ p1q1.rgb = p1q0.rgb; }

		if(p0q1.a > p0q0.a)		{ p0q0.rgb = p0q1.rgb; }
		if(p0q1.a > p1q1.a)		{ p1q1.rgb = p0q1.rgb; }

		if(p1q1.a > p0q1.a)		{ p0q1.rgb = p1q1.rgb; }
		if(p1q1.a > p1q0.a)		{ p1q0.rgb = p1q1.rgb; }
	}

	// Interpolation in X direction.
	float4 pInterp_q0 = mix( p0q0, p1q0, a ); // Interpolates top row in X direction.
	float4 pInterp_q1 = mix( p0q1, p1q1, a ); // Interpolates bottom row in X direction.

	return mix( pInterp_q0, pInterp_q1, b ); // Interpolate in Y direction.
}

static float4 GetTextureValue(thread FragState& fs, constant R3DUniforms& u,
                              array<texture2d<ushort, access::read>, 2> banks)
{
	float lod = -log2(fs.fragCoord.w * fs.fragCoord.w * fs.fsLODBase);
	float numLevels = floor(log2(min(float(u.baseTexInfo.z), float(u.baseTexInfo.w)))) - 1.0;	// r3d only generates down to 2:2 for square textures, otherwise its the min dimension
	float fLevel = clamp(lod, 0.0, numLevels);

	int iLevel = int(fLevel);


	int2 tex1Pos = GetTexturePosition(iLevel, u.baseTexInfo.xy);
	int2 tex1Size = GetTextureSize(iLevel, u.baseTexInfo.zw);
	float4 tex1Data = texBiLinear(banks[u.texturePage], u, u.textureWrapMode, float2(tex1Size), tex1Pos, fs.fsTexCoord, iLevel);

	// init second texel with blank data to avoid any potentially undefined behavior
	float4 tex2Data = float4(0.0);

	float blendFactor = 0.0;

	// if LOD < 0, no need to blend with next mipmap level; slight performance boost
	//  while at it, just generalize to all cases where only one mip level needs to be touched
	float ffL = fract(fLevel);
	if (ffL > 0.0)
	{
		int2 tex2Pos = GetTexturePosition(iLevel+1, u.baseTexInfo.xy);
		int2 tex2Size = GetTextureSize(iLevel+1, u.baseTexInfo.zw);
		tex2Data = texBiLinear(banks[u.texturePage], u, u.textureWrapMode, float2(tex2Size), tex2Pos, fs.fsTexCoord, iLevel+1);

		blendFactor = ffL;
	}
	else if ((u.flags & F_MICRO_TEXTURE) != 0 && lod < 0.0)
	{
		const float scaleIndex[4] = { 2.0, 4.0, 16.0, 256.0 };		// unsure if minLOD=4 has 256x scale? No games appear to use it
		float2 scale = (float2(u.baseTexInfo.zw) / 128.0) * scaleIndex[int(u.microTextureMinLOD)];

		// microtextures are always 128x128 and only use LOD 0 mipmap
		int2 tex2Pos = GetMicroTexturePos(u.microTextureID);
		tex2Data = texBiLinear(banks[(u.texturePage+1)&1], u, int2(0), float2(128.0), tex2Pos, fs.fsTexCoord * scale, 0);

		blendFactor = -lod * exp2(-u.microTextureMinLOD) * 0.5;
		blendFactor = min(blendFactor, 0.5);
	}

	tex1Data = mix(tex1Data, tex2Data, blendFactor);

	if((u.flags & F_TEXTURE_INVERTED) != 0) {
		tex1Data.rgb = float3(1.0) - tex1Data.rgb;
	}

	bool alphaTest		= (u.flags & F_ALPHA_TEST) != 0;
	bool textureAlpha	= (u.flags & F_TEXTURE_ALPHA) != 0;

	if (alphaTest) {
		if (tex1Data.a < (32.0/255.0)) {
			fs.discarded = true;
		}
	}

	if(textureAlpha) {
		if((u.flags & F_DISCARD_ALPHA) != 0) {	// opaque 1st pass
			if (tex1Data.a < 1.0) {
				fs.discarded = true;
			}
		}
		else {								// transparent 2nd pass
			if ((tex1Data.a * fs.fsColor.a) >= 1.0) {
				fs.discarded = true;
			}
		}
	}

	if (textureAlpha == false && alphaTest == false) {
		tex1Data.a = 1.0;
	}

	return tex1Data;
}

static void Step15Luminous(thread float4& colour, thread FragState& fs, constant R3DUniforms& u)
{
	// luminous polys seem to behave very differently on step 1.5 hardware
	// when fixed shading is enabled the colour is modulated by the vp ambient + fixed shade value
	// when disabled it appears to be multiplied by 1.5, presumably to allow a higher range
	if((u.flags & F_STEP15_LUMINOUS) != 0) {
		if((u.flags & F_LIGHT_ENABLED) == 0) {
			if((u.flags & F_FIXED_SHADING) != 0) {
				colour.rgb *= 1.0 + fs.fsFixedShade + u.lighting1.y;
			}
			else {
				colour.rgb *= 1.5;
			}
		}
	}
}

static float CalcFog(thread FragState& fs, constant R3DUniforms& u)
{
	float z		= -fs.fsViewVertex.z;
	float fog	= u.fogIntensity * clamp(u.fogStart + z * u.fogDensity, 0.0, 1.0);

	return fog;
}

static float Sqr(float a)
{
	return a*a;
}

static float SqrLength(float2 a)
{
	return a.x*a.x + a.y*a.y;
}

static void ComputeSpotlight(float4 fragCoord, float4 spotEllipse, float2 spotRange, float viewZ,
	thread float& lobeEffect, thread float& lobeFogEffect)
{
	// Ellipse falloff
	float ellipse = SqrLength((fragCoord.xy - spotEllipse.xy) / spotEllipse.zw);
	ellipse = 1.0 - ellipse;
	ellipse = max(0.0, ellipse);

	// Spotlight enable
	float enable = step(spotRange.x, -viewZ);

	// Spotlight range falloff
	float range = 0.0;

	if (spotRange.y != 0.0) {
		float absExtent = abs(spotRange.y);

		float d = spotRange.x + absExtent + viewZ;
		d = min(d, 0.0);

		float inv_r = 1.0 / (1.0 + absExtent);

		// inverse-linear falloff
		range = 1.0 / Sqr(d * inv_r - 1.0);
		range *= enable;
	}

	// Final outputs
	lobeEffect    = range * ellipse;
	lobeFogEffect = enable * ellipse;
}

static void WriteOutputs(thread R3DFragOut& o, float4 colour, int layer)
{
	float4 blank = float4(0.0);

	if(layer==LayerColour) {
		colour.a = 1.0;		// always have alpha of 1
		o.out0 = colour;
		o.out1 = blank;
		o.out2 = blank;
	}
	else if(layer==LayerTrans0) {
		o.out0 = blank;
		o.out1 = colour;
		o.out2 = blank;
	}
	else if(layer==LayerTrans1) {
		o.out0 = blank;
		o.out1 = blank;
		o.out2 = colour;
	}
}

static float QuadraticInterpolation(thread FragState& fs, QuadOut fs_in, constant R3DUniforms& u)
{
	float2 inV[4]		= { fs_in.v01.xy, fs_in.v01.zw, fs_in.v23.xy, fs_in.v23.zw };
	float inArea[4]		= { fs_in.area.x, fs_in.area.y, fs_in.area.z, fs_in.area.w };
	float inOneOverW[4]	= { fs_in.oneOverW.x, fs_in.oneOverW.y, fs_in.oneOverW.z, fs_in.oneOverW.w };
	float3 inViewVertex[4]	= { fs_in.viewVertex0, fs_in.viewVertex1, fs_in.viewVertex2, fs_in.viewVertex3 };
	float3 inViewNormal[4]	= { fs_in.viewNormal0, fs_in.viewNormal1, fs_in.viewNormal2, fs_in.viewNormal3 };
	float2 inTexCoord[4]	= { fs_in.texCoord01.xy, fs_in.texCoord01.zw, fs_in.texCoord23.xy, fs_in.texCoord23.zw };
	float inFixedShade[4]	= { fs_in.fixedShade.x, fs_in.fixedShade.y, fs_in.fixedShade.z, fs_in.fixedShade.w };

	// init interpolated vertex attributes
	fs.fsViewVertex	= float3(0.0);
	fs.fsViewNormal	= float3(0.0);
	fs.fsTexCoord	= float2(0.0);
	fs.fsFixedShade	= 0.0;

	float interp_oneOverW	= 0.0;
	float uSum				= 0.0;

	const float hpFMin		= 0.00006103515625;		// half precision FLT_MIN
	bool extreme_edge		= (fs_in.barycentricCoords.w > 0.5 || fs_in.barycentricCoords.x <= hpFMin || fs_in.barycentricCoords.z <= hpFMin); // w: triangle

	float uu[4];
	if(extreme_edge)
	{
		for (int i=0; i<4; i++) {
			int b = (i == 3) ? 1  // 2nd triangle
			                 : i; // 1st triangle
			float baryCoord	= abs(fs_in.barycentricCoords[b]);
			uu[i] = baryCoord / inOneOverW[i];
			if((i == 1 && !(fs_in.barycentricCoords.y>0.0)) // ignore u[1] if 2nd triangle
			 ||(i == 3 &&  (fs_in.barycentricCoords.y>0.0)) // ignore u[3] if 1st triangle
			 ||(baryCoord <= hpFMin))
			{
				uu[i] = 0.0;
			}
		}
	}
	else
	{
		for (int i=0; i<4; i++)
			uu[i] = length(inV[i]) * sign(inOneOverW[i]); // is w[i] negative?

		float t[4];
		for (int i=0; i<4; i++) {
			int i_next = (i+1)%4;
			if(inArea[i]==0.0) t[i] = 0.0; // check for zero area to avoid div by zero
			else               t[i] = fma(uu[i],uu[i_next], -dot(inV[i],inV[i_next])) / inArea[i];
		}

		int lambdaSignCount = 0; // to discard fragments if all the weights are neither all negative nor all positive (=outside the convex/concave/crossed quad).

		for (uint i=0; i<4; i++) {
			uint i_prev = (i-1)%4;
			uu[i] = (t[i_prev] + t[i]) / uu[i];
			lambdaSignCount += (t[i_prev] < -t[i]) ? -1 : 1;
		}

		if (lambdaSignCount == 0) {
			fs.discarded = true;
			return 0.0;
		}

		for (int i=0; i<4; i++) {
			interp_oneOverW	= fma(inOneOverW[i], uu[i], interp_oneOverW);
			uSum           += uu[i];
		}
	}

	// shared computations
	for (int i=0; i<4; i++) {
		fs.fsViewVertex	= fma(inViewVertex[i], float3(uu[i]), fs.fsViewVertex);
		fs.fsViewNormal	= fma(inViewNormal[i], float3(uu[i]), fs.fsViewNormal);
		fs.fsTexCoord	= fma(inTexCoord[i], float2(uu[i]), fs.fsTexCoord);
		fs.fsFixedShade	= fma(inFixedShade[i], uu[i], fs.fsFixedShade);
	}

	// finalize the 2 cases
	if(extreme_edge)
	{
		float w = (u.projMat * float4(fs.fsViewVertex,1.0)).w;

		interp_oneOverW = 1.0 / w;
		uSum = 1.0;
	}
	else
	{
		float inv = 1.0/interp_oneOverW;
		fs.fsViewVertex *= inv;
		fs.fsViewNormal *= inv;
		fs.fsTexCoord   *= inv;
		fs.fsFixedShade *= inv;
	}

	float4 clipPos;		// "vertex" is a keyword in Metal
	float depth;

	// dirty hack for co-planar polys that really need 100% identical values to depth test correctly
	if(inOneOverW[0]==inOneOverW[1] &&
	   inOneOverW[1]==inOneOverW[2] &&
	   inOneOverW[2]==inOneOverW[3]) {

		fs.fsViewVertex.z	= inViewVertex[0].z / inOneOverW[0];
		clipPos				= u.projMat * float4(fs.fsViewVertex,1.0);
		depth				= clipPos.z / clipPos.w;
	}
	else {
		clipPos.z		= u.projMat[2][2] * fs.fsViewVertex.z + u.projMat[3][2];		// standard projMat * vertex - but just using Z components
		depth			= clipPos.z * (interp_oneOverW/uSum);
	}

	fs.fsColor		= fs_in.color;
	fs.fsLODBase	= fs_in.LODBase;

	return depth * u.layerDepthBias;
}

fragment R3DFragOut r3dQuadFragment(QuadOut fs_in [[stage_in]],
                                    constant R3DUniforms& u [[buffer(1)]],
                                    array<texture2d<ushort, access::read>, 2> banks [[texture(0)]])
{
	R3DFragOut o;
	FragState fs;
	fs.fragCoord = fs_in.position;
	fs.discarded = false;

	float4 tex1Data;
	float4 colData;
	float4 finalData;
	float4 fogData;

	o.depth = QuadraticInterpolation(fs, fs_in, u);	// calculate our vertex attributes
	if (fs.discarded) {
		discard_fragment();
		return o;
	}

	fogData = float4(u.fogColour.rgb * u.fogAmbient, CalcFog(fs, u));
	tex1Data = float4(1.0, 1.0, 1.0, 1.0);

	if((u.flags & F_TEXTURE_ENABLED) != 0) {
		tex1Data = GetTextureValue(fs, u, banks);
		if (fs.discarded) {
			discard_fragment();
			return o;
		}
	}

	colData = fs.fsColor;
	Step15Luminous(colData, fs, u);			// no-op for step 2.0+
	finalData = tex1Data * colData;

	if (finalData.a < (1.0/32.0)) {		// basically chuck out any totally transparent pixels value = 1/16 the smallest transparency level h/w supports
		discard_fragment();
		return o;
	}

	float lobeEffect;
	float lobeFogEffect;
	ComputeSpotlight(fs.fragCoord, u.spotEllipse, u.spotRange.xy, fs.fsViewVertex.z, lobeEffect, lobeFogEffect);

	if ((u.flags & F_LIGHT_ENABLED) != 0) {
		float3 lightIntensity;
		float3 sunVector;     // sun lighting vector (as reflecting away from vertex)
		float  sunFactor;     // sun light projection along vertex normal (0.0 to 1.0)

		// Sun angle
		sunVector = u.lighting0.xyz;

		// Compute diffuse factor for sunlight
		if((u.flags & F_FIXED_SHADING) != 0) {
			sunFactor = fs.fsFixedShade;
		}
		else {
			sunFactor = dot(sunVector, fs.fsViewNormal);
		}

		// Clamp ceil, fix for upscaled models without "modelScale" defined
		sunFactor = clamp(sunFactor,-1.0,1.0);

		// Optional clamping, value is allowed to be negative
		// We suspect that translucent polygons are always clamped (e.g. lasers in Daytona 2)
		if((u.flags & F_SUN_CLAMP) != 0 || finalData.a<0.999) {
			sunFactor = max(sunFactor,0.0);
		}

		// Total light intensity: sum of all components
		lightIntensity = float3(sunFactor*u.lighting1.x + u.lighting1.y);   // diffuse + ambient

		lightIntensity += u.spotColor.rgb*lobeEffect;

		// Upper clamp is optional, step 1.5+ games will drive brightness beyond 100%
		if((u.flags & F_INTENSITY_CLAMP) != 0) {
			lightIntensity = min(lightIntensity,1.0);
		}

		finalData.rgb *= lightIntensity;

		// for now assume fixed shading doesn't work with specular
		if ((u.flags & F_SPECULAR_ENABLED) != 0) {

			float specularFactor;

			if ((u.flags & F_SMOOTH_SHADING) != 0)
			{
				// Always clamp floor to zero
				float NdotL = max(0.0, sunFactor);

				const float expIndex[4]  = { 8.0, 16.0, 32.0, 64.0 };
				const float multIndex[4] = { 1.6, 1.6, 2.4, 3.2 };
				float exponent = expIndex[int(u.shininess)];

				specularFactor = pow(NdotL, exponent);
				specularFactor *= multIndex[int(u.shininess)];
			}
			else
			{
				// flat shaded polys use Phong reflection model (R dot V) without any exponent or multiplier
				// V = (0.0, 0.0, 1.0) is used by Model 3 as a fast approximation, so R dot V = R.z
				float3 R = reflect(-sunVector, fs.fsViewNormal);
				specularFactor = max(0.0, R.z);
			}

			specularFactor *= u.specularValue;
			specularFactor *= u.lighting1.x;

			if (colData.a < 1.0) {
				/// Specular hi-light affects translucent polygons alpha channel ///
				finalData.a = max(finalData.a, specularFactor);
			}

			finalData.rgb += specularFactor;
		}
	}

	// Final clamp: we need it for proper shading in dimmed light and dark ambients
	finalData.rgb = min(finalData.rgb, float3(1.0));

	// Spotlight on fog
	float3 lSpotFogColor = u.spotFogColor.rgb * u.fogAttenuation * u.fogColour.rgb * lobeFogEffect;

	// Fog & spotlight applied
	finalData.rgb = mix(finalData.rgb, fogData.rgb + lSpotFogColor, fogData.a);

	// Write outputs to colour buffers
	WriteOutputs(o, finalData, u.colourLayer);
	return o;
}

// ---- full-screen quads (scroll fog, the layer composite, the 2D layers) ----

struct FullOut
{
	float4 position [[position]];
	float2 texCoord;
};

struct FullUniforms
{
	float4	offsetScale;	// clip space (OpenGL's) offset and scale: an OpenGL viewport other than the whole target
};

vertex FullOut fullVertex(uint vid [[vertex_id]], constant FullUniforms& u [[buffer(0)]])
{
	const float4 vertices[4] = { float4(-1.0, -1.0, 0.0, 1.0),
	                             float4(-1.0,  1.0, 0.0, 1.0),
	                             float4( 1.0, -1.0, 0.0, 1.0),
	                             float4( 1.0,  1.0, 0.0, 1.0) };
	const float2 texCoords[4] = { float2(0.0, 1.0), // flip upside down (Shaders2D.h)
	                              float2(0.0, 0.0),
	                              float2(1.0, 1.0),
	                              float2(1.0, 0.0) };
	FullOut o;
	float4 p = vertices[vid % 4];
	p.xy = p.xy * u.offsetScale.zw + u.offsetScale.xy;
	o.position = GlPositionToMetal(p);
	o.texCoord = texCoords[vid % 4];
	return o;
}

struct FogUniforms
{
	float4	fogColour;
	float4	spotFogColor;		// rgb
	float4	spotEllipse;
	float	fogAttenuation;
	float	fogAmbient;
	float	pad[2];
};

struct FogOut
{
	float4 out0 [[color(0)]];
	float4 out1 [[color(1)]];
	float4 out2 [[color(2)]];
};

fragment FogOut scrollFogFragment(FullOut in [[stage_in]], constant FogUniforms& u [[buffer(0)]])
{
	// Scroll fog base color
	float3 lFogColor = u.fogColour.rgb * u.fogAmbient;

	// Spotlight on fog (area)
	float2 position = u.spotEllipse.xy;
	float2 size = u.spotEllipse.zw;
	float ellipse = length((in.position.xy - position) / size);
	ellipse = ellipse * ellipse;			// decay rate = square of distance from center
	ellipse = 1.0 - ellipse;				// invert
	ellipse = max(0.0, ellipse);			// clamp

	// Spotlight on fog (color)
	float3 lSpotFogColor = mix(float3(0.0), u.spotFogColor.rgb * ellipse * u.fogColour.rgb, u.fogAttenuation);

	// Scroll fog density
	float4 scrollFog = float4(lFogColor + lSpotFogColor, u.fogColour.a);

	FogOut o;
	o.out0 = scrollFog;
	o.out1 = float4(0.0);
	o.out2 = float4(0.0);
	return o;
}

fragment float4 baseLayerFragment(FullOut in [[stage_in]], texture2d<float, access::read> tex1 [[texture(0)]])
{
	uint2 tc = uint2(int2(in.position.xy));
	return tex1.read(tc);
}

fragment float4 transLayerFragment(FullOut in [[stage_in]],
                                   texture2d<float, access::read> tex1 [[texture(0)]],
                                   texture2d<float, access::read> tex2 [[texture(1)]])
{
	uint2 tc = uint2(int2(in.position.xy));
	float4 colTrans1 = tex1.read(tc);
	float4 colTrans2 = tex2.read(tc);

	// if both transparency layers overlap, the result is opaque
	if (colTrans1.a * colTrans2.a > 0.0) {
		float3 mixCol = mix(colTrans1.rgb, colTrans2.rgb, (colTrans2.a + (1.0 - colTrans1.a)) / 2.0);
		return float4(mixCol, 1.0);
	}
	else if (colTrans1.a > 0.0) {
		return colTrans1;
	}
	else {
		return colTrans2;		// if alpha is zero it will have no effect anyway
	}
}

fragment float4 solidFragment(FullOut in [[stage_in]], constant float4& colour [[buffer(0)]])
{
	return colour;
}

fragment float4 layer2DFragment(FullOut in [[stage_in]], texture2d<float> tex1 [[texture(0)]])
{
	constexpr sampler s(filter::nearest, address::clamp_to_edge);
	return tex1.sample(s, in.texCoord);
}

)msl";
