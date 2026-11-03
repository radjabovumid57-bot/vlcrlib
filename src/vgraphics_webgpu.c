#include "vplatform.h"
#if !VLCRLIB_USE_WEBGPU
#else
#include "vgraphics.h"
#include "vcore.h"
#include "vmath.h"
#include "vibl.h"
#include "vrender_graph_webgpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "stb_image_write.h"
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu.h>
#else
#error WebGPU needs Emscripten
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static WGPUInstance g_inst; static WGPUAdapter g_adapter; static WGPUDevice g_dev; static WGPUQueue g_q;
static WGPUSurface g_surf; static WGPUSwapChain g_swapchain;
static WGPUTextureFormat g_fmt = WGPUTextureFormat_BGRA8Unorm;
static WGPUTexture g_depth; static WGPUTextureView g_depthV;
static WGPUTexture g_sceneTex; static WGPUTextureView g_sceneView;
static WGPUTexture g_finalTex; static WGPUTextureView g_finalView; static WGPUBindGroup g_bgFinal;
static WGPUTexture g_bloomTex; static WGPUTextureView g_bloomView;
static WGPUTexture g_ssaoTex; static WGPUTextureView g_ssaoView;
static WGPUTexture g_taaTex[2]; static WGPUTextureView g_taaView[2];
static WGPUTexture g_velTex; static WGPUTextureView g_velView;
static WGPUBindGroupLayout g_bglTaa,g_bglVel; static WGPURenderPipeline g_pTaa,g_pVel;
static WGPUBuffer g_taaUBO,g_velUBO,g_velVB;
static int g_taaEnabled,g_taaCur,g_taaFrame; static Matrix g_prevVP;
static Matrix g_prevCmdModel[512]; static int g_prevCmdMid[512],g_prevCmdValid[512];
static uint64_t g_frameIndex; static int g_statDraws,g_statShadowDraws; static uint64_t g_statTris;
static double g_frameStart,g_cpuFrameMs; static float g_exposure=1.0f;
static WGPUFeatureName g_reqFeatures[5]; static WGPURequiredLimits g_reqLimits; static int g_bcSupported,g_etc2Supported,g_astcSupported,g_f32Filterable; static int g_tsSupported,g_tsIdx,g_tsPrev,g_tsMapped[2],g_tsSampled;
static WGPUQuerySet g_tsSet; static WGPUBuffer g_tsBuf[2],g_tsResolve[2]; static float g_gpuFrameMs;
static WGPURenderPipeline g_p2, g_pbr, g_plines, g_pcomp, g_pcompFxaa, g_pblit, g_pbright, g_pcompBloom, g_pssao, g_pcompSsao;
static WGPUBindGroupLayout g_bglp,g_bgls,g_bgls2; static WGPUBindGroup g_bgBloom,g_bgSsao,g_bgSsaoComp;
static WGPUBuffer g_ssaoUBO;
static int g_fxaa=1,g_bloomEnabled,g_ssaoEnabled; static float g_ssaoRadius=0.6f,g_ssaoIntensity=1.5f;
static WGPUBindGroupLayout g_bglc; static WGPUBindGroup g_bgc;
static VrgwGraph *g_postGraph;
static int g_pgScene=-1,g_pgDepth=-1,g_pgVel=-1,g_pgTaa=-1,g_pgFinal=-1,g_pgSsao=-1,g_pgBloom=-1;
enum { PG_SSAO, PG_BLOOM, PG_VELOCITY, PG_TAA, PG_COMPOSITE, PG_PASS_COUNT };
static WGPUBuffer g_vb2, g_vb3, g_ubo;
static WGPUBindGroupLayout g_bgl,g_bglGlob,g_bglMat;
static WGPUBindGroupLayout g_bgl2;
static WGPUSampler g_samp; static WGPUTexture g_wtex; static WGPUTextureView g_wview;
static WGPUSampler g_iblSamp;
static WGPUTexture g_iblIrrTex,g_iblPreTex,g_iblBrdfTex;
static WGPUTextureView g_iblIrrView,g_iblPreView,g_iblBrdfView;
static WGPUTexture g_iblFallbackCubeTex,g_iblFallback2DTex;
static WGPUTextureView g_iblFallbackCubeView,g_iblFallback2DView;
static int g_iblLoaded,g_iblMips; static float g_iblIntensity;
static WGPUTexture g_shadowTex; static WGPUTextureView g_shadowView; static WGPUSampler g_shadowSamp;
static WGPUTexture g_spotTex; static WGPUTextureView g_spotView;
static WGPUBuffer g_spotUBO,g_spotVB; static Matrix g_spotVP;
static WGPUTexture g_spot2Tex; static WGPUTextureView g_spot2View;
static WGPUBuffer g_spot2UBO,g_spot2VB; static Matrix g_spot2VP; static int g_spot2On;
static WGPUTexture g_spot3Tex; static WGPUTextureView g_spot3View;
static WGPUBuffer g_spot3UBO,g_spot3VB; static Matrix g_spot3VP;
static WGPUTexture g_spot4Tex; static WGPUTextureView g_spot4View;
static WGPUBuffer g_spot4UBO,g_spot4VB; static Matrix g_spot4VP;
static WGPUTexture g_pointTex; static WGPUTextureView g_pointView,g_pointFaceView[6]; static WGPUSampler g_pointRawSamp;
static WGPUBuffer g_pointUBO[6],g_pointVB[6]; static Matrix g_pointVP[6];
static WGPUTexture g_point2Tex; static WGPUTextureView g_point2View,g_point2FaceView[6];
static WGPUBuffer g_point2UBO[6],g_point2VB[6]; static Matrix g_point2VP[6];
static WGPUTexture g_point3Tex; static WGPUTextureView g_point3View,g_point3FaceView[6];
static WGPUBuffer g_point3UBO[6],g_point3VB[6]; static Matrix g_point3VP[6];
static WGPUTexture g_pointArrTex; static WGPUTextureView g_pointArrView,g_pointArrFace[4][6];
static Matrix g_point4VP[6];
static int g_spotOn; static Vector3 g_spotPos={3,4,3},g_spotDir={-0.5f,-0.7f,-0.5f};
static Color g_spotColor={255,220,180,255}; static float g_spotIntensity=2.0f,g_spotRange=25.0f,g_spotCos=0.8f;
typedef struct{Vector3 pos;Color color;float intensity,range;int on;} PointL;
static PointL g_points[4];
typedef struct{Vector3 pos,dir;float cr,cg,cb,range,cos;int on;} ExtraSpot;
static ExtraSpot g_extraSpots[3];
static WGPUBuffer g_shadowUBO,g_shadowVB,g_shadowUBO2,g_shadowVB2,g_shadowUBO3,g_shadowVB3,g_shadowUBO4,g_shadowVB4;
static WGPUBindGroupLayout g_bglShadow; static WGPURenderPipeline g_pShadow;
static Matrix g_shadowVP,g_shadowVP2,g_shadowVP3,g_shadowVP4; static int g_shadowEnabled,g_shadowValid;
static float g_shadowSplits[4]={6.0f,10.0f,16.0f,1e9f};
static float g_shadowBias=0.003f,g_shadowStrength=0.7f,g_shadowTexel=1.0f/1024.0f,g_shadowSoftness=1.5f;
static void releaseIbl(void);
static int gw=960,gh=540; static float g_clear[4]={0.12f,0.13f,0.16f,1};
static Matrix g_view,g_proj; static Vector3 g_cam; static int g_ok;
static int g_webgpuErrors;
static int g_lon=1; static Vector3 g_ld={-0.4f,-0.8f,-0.3f};
static float g_lc[3]={1,0.98f,0.95f},g_li=1.2f,g_ac[3]={0.25f,0.28f,0.35f},g_ai=0.35f;

bool vgraphics_IsHardwareRayTracingAvailable(void) { return false; }

typedef struct{float x,y,u,v,r,g,b,a;} V2;
static V2 b2[16384]; static int n2;
/* 2D draw batches grouped by texture */
typedef struct{unsigned texId;int start,count;} B2Cmd;
static B2Cmd b2cmds[256]; static int nb2c;
static unsigned b2cur_tex;
typedef struct{float x,y,z,nx,ny,nz,u,v,tx,ty,tz,tw,r,g,b,a;} V3;
static V3 b3[65536]; static int n3; static V3 bl[8192]; static int nl;

typedef struct{float mvp[16],model[16],ld[4],lc[4],amb[4],cam[4],bc[4],mr[4],ibl[4],shadowVP[16],shadowParams[4],shadowVP2[16],shadowCfg[4],shadowVP3[16],shadowVP4[16],shadowSplits[4],spotVP[16],spotLight[4],spotDir[4],spotColor[4],spotParams[4],pointPos[16],pointColor[16],pointVP[96],pointShadowParams[4],extraSpotPos[16],extraSpotDir[16],extraSpotColor[16],extraSpotParams[16],spot2VP[16],spot2Light[4],spot2Dir[4],spot2Color[4],spot2Params[4],spot3VP[16],spot3Light[4],spot3Dir[4],spot3Color[4],spot3Params[4],spot4VP[16],spot4Light[4],spot4Dir[4],spot4Color[4],spot4Params[4],point2VP[96],point2Params[4],point3VP[96],point3Params[4],point4VP[96],point4Params[4],exposure[4];} UBO;
typedef struct{float invProj[16],params[4];} SSAOUBO;
typedef struct{float prevVP[16],invVP[16],params[4];} TAAUBO;
typedef struct{float curVP[16],prevVP[16],curModel[16],prevModel[16];} VELUBO;

#define MT 256
#define MM 256
#define MO 64
#define MA 128
/* Queue writes execute before the submitted command buffer, so each draw needs
   its own UBO slot and vertex range instead of overwriting shared buffers. */
#define MAX_DRAWS 512
#define UBO_STRIDE 3072
#define MAX_FRAME_VERTS 131072
typedef struct{int u;WGPUTexture t;WGPUTextureView v;WGPUSampler samp;int w,h,mips,srgb,bc5;} WT;
static WT tex[MT]; static int tn;
typedef struct{unsigned a,m,n,o,e;WGPUBindGroup bg;} BGC;
static BGC bgCache[128]; static int nbg;
static void bgClear(void){for(int i=0;i<nbg;++i)if(bgCache[i].bg)wgpuBindGroupRelease(bgCache[i].bg);memset(bgCache,0,sizeof bgCache);nbg=0;}
typedef struct{int u;V3*v;int vc;uint32_t*i;int ic;} WM;
static WM meshes[MM]; static int mn;
typedef struct{int u;Color bc;float met,rou;unsigned a,m,n,o,e;} WA;
static WA mats[MA]; static int man;
typedef struct{int u;int mid[32],aid[32];Matrix xf[32];int c;} WO;
static WO models[MO]; static int mon;
typedef struct{int mid,aid;Matrix model;Color tint;} DC;
static DC cmds[MAX_DRAWS]; static int nc;

/* ---- Skinning -----------------------------------------------------
   Mirrors the Vulkan tables: one skeleton per skinned model, a global
   animation list and per-frame command/bone data. Bone matrices are
   evaluated on the CPU and uploaded as a 256x mat4 uniform block; the
   vertex shader blends them (four influences). */
#define VLCR_SKIN_MODELS 64
#define VLCR_SKIN_ANIMS  128
#define VLCR_SKIN_DRAWS  64
#define VLCR_SKIN_BONES  256
#define VLCR_SKIN_BONE_BYTES   (VLCR_SKIN_BONES*64)
#define VLCR_SKIN_CURPREV_BYTES (VLCR_SKIN_BONES*64*2)
#define VLCR_SKIN_MAX_VERTS 65536

/* Skinned vertex: position/normal/uv + joint indices + weights. Vertex
   colour is not stored; the skin vertex stage emits white. */
typedef struct{float x,y,z,nx,ny,nz,u,v,tx,ty,tz,tw;uint32_t j[4];float w[4];} SV3;

typedef struct{char name[64];int parent;Matrix inv;Vector3 rt;Quaternion rr;Vector3 rs;} SkBone;
typedef struct{int boneCount;SkBone bones[VLCR_SKIN_BONES];} SkSkel;
typedef struct{float t;Vector3 v;} SkVecKey;
typedef struct{float t;Quaternion q;} SkQuatKey;
typedef struct{SkVecKey*pos;int posCount;SkQuatKey*rot;int rotCount;SkVecKey*scale;int scaleCount;} SkTrack;
typedef struct{char name[64];float duration;SkSkel*skel;SkTrack*tracks;int trackCount;} SkAnim;
typedef struct{
 int u;SkSkel*skel;Material mat;
 int*animIds;int animCount,animCap;
 SkinnedVertex3D*verts;int vc;uint32_t*idx;int ic;
 Vector3 bmin,bmax;int hasBounds;
 Matrix prevBones[VLCR_SKIN_BONES];Matrix prevModel;int prevValid;
} SkModel;
typedef struct{int sid;Matrix model;Color tint;} SKC;

static SkModel* skModels;
static SkAnim* skAnims; static int skAnimN,skAnimCap;
static SKC skCmds[VLCR_SKIN_DRAWS]; static int nskc;
static Matrix skDrawBones[VLCR_SKIN_DRAWS][VLCR_SKIN_BONES];
static SV3* skv; static int skvCap;

static WGPURenderPipeline g_pskin,g_pshadowSkin,g_pVelSkin;
static WGPUBindGroupLayout g_bglSkin,g_bglSkinVel;
static WGPUBindGroup g_bgSkin,g_bgSkinVel;
static WGPUBuffer g_skiVB,g_skiUBO,g_velSkVB,g_velSkUBO;
static WGPUShaderModule g_skinMod;
static void skFreeAnim(SkAnim*);
static void skFreeModelEntry(SkModel*);

static const char* S2=
"struct O{@builtin(position)p:vec4f,@location(0)uv:vec2f,@location(1)c:vec4f,};"
"@vertex fn vs(@location(0)p:vec2f,@location(1)uv:vec2f,@location(2)c:vec4f)->O{"
"var o:O;o.p=vec4f(p,0.0,1.0);o.uv=uv;o.c=c;return o;}"
"@group(0)@binding(0) var samp:sampler;@group(0)@binding(1) var tex:texture_2d<f32>;"
"@fragment fn fs(i:O)->@location(0)vec4f{"
"return textureSample(tex,samp,i.uv)*i.c;}";
static const char* SP=
"struct U{mvp:mat4x4f,model:mat4x4f,ld:vec4f,lc:vec4f,amb:vec4f,cam:vec4f,bc:vec4f,mr:vec4f,ibl:vec4f,shadowVP:mat4x4f,shadowParams:vec4f,shadowVP2:mat4x4f,shadowCfg:vec4f,shadowVP3:mat4x4f,shadowVP4:mat4x4f,shadowSplits:vec4f,spotVP:mat4x4f,spotLight:vec4f,spotDir:vec4f,spotColor:vec4f,spotParams:vec4f,pointPos:array<vec4f,4>,pointColor:array<vec4f,4>,pointVP:array<mat4x4f,6>,pointShadowParams:vec4f,extraSpotPos:array<vec4f,4>,extraSpotDir:array<vec4f,4>,extraSpotColor:array<vec4f,4>,extraSpotParams:array<vec4f,4>,spot2VP:mat4x4f,spot2Light:vec4f,spot2Dir:vec4f,spot2Color:vec4f,spot2Params:vec4f,spot3VP:mat4x4f,spot3Light:vec4f,spot3Dir:vec4f,spot3Color:vec4f,spot3Params:vec4f,spot4VP:mat4x4f,spot4Light:vec4f,spot4Dir:vec4f,spot4Color:vec4f,spot4Params:vec4f,point2VP:array<mat4x4f,6>,point2Params:vec4f,point3VP:array<mat4x4f,6>,point3Params:vec4f,point4VP:array<mat4x4f,6>,point4Params:vec4f,exposure:vec4f,};"
"@group(0)@binding(0) var<uniform> u:U;"
"@group(2)@binding(0) var s:sampler;@group(2)@binding(1) var ta:texture_2d<f32>;@group(2)@binding(2) var tm:texture_2d<f32>;"
"@group(1)@binding(4) var irrTex:texture_cube<f32>;@group(1)@binding(5) var preTex:texture_cube<f32>;"
"@group(1)@binding(6) var brdfTex:texture_2d<f32>;@group(1)@binding(7) var iblSamp:sampler;"
"@group(1)@binding(8) var shadowMap:texture_depth_2d;@group(1)@binding(9) var shadowSamp:sampler_comparison;"
"@group(1)@binding(10) var spotMap:texture_depth_2d;@group(1)@binding(11) var pointMap:texture_depth_cube_array;"
"@group(2)@binding(3) var normalTex:texture_2d<f32>;@group(2)@binding(4) var aoTex:texture_2d<f32>;@group(2)@binding(5) var emTex:texture_2d<f32>;"
"@group(1)@binding(15) var pointMapRaw:texture_depth_cube_array;@group(1)@binding(16) var pointRawSamp:sampler;@group(1)@binding(17) var spotMap2:texture_depth_2d;@group(1)@binding(18) var spotMap3:texture_depth_2d;@group(1)@binding(19) var spotMap4:texture_depth_2d;"
"struct O{@builtin(position)p:vec4f,@location(0)n:vec3f,@location(1)w:vec3f,@location(2)uv:vec2f,@location(3)c:vec4f,@location(4)t:vec4f,};"
"@vertex fn vs(@location(0)p:vec3f,@location(1)n:vec3f,@location(2)uv:vec2f,@location(3)c:vec4f,@location(4)t:vec4f)->O{"
"var o:O;let wp=u.model*vec4f(p,1);o.p=u.mvp*vec4f(p,1);o.w=wp.xyz;o.n=normalize((u.model*vec4f(n,0)).xyz);o.uv=uv;o.c=c;o.t=t;return o;}"
"fn Fsch(c:f32,F0:vec3f)->vec3f{return F0+(1-F0)*pow(clamp(1-c,0,1),5);}"
"fn shadowAt(wp:vec3f,ndl:f32)->f32{"
"if(u.shadowParams.w<0.5){return 1.0;}"
"let dist=distance(u.cam.xyz,wp);var c:vec4f;var ox=0.0;var oy=0.0;var sc=u.shadowSplits.x;"
"if(dist<u.shadowSplits.x){c=u.shadowVP*vec4f(wp,1.0);}"
"else if(dist<u.shadowSplits.y){c=u.shadowVP2*vec4f(wp,1.0);ox=0.5;sc=u.shadowSplits.y;}"
"else if(dist<u.shadowSplits.z){c=u.shadowVP3*vec4f(wp,1.0);oy=0.5;sc=u.shadowSplits.z;}"
"else{c=u.shadowVP4*vec4f(wp,1.0);ox=0.5;oy=0.5;sc=1e9;}"
"let ndc=c.xyz/c.w;"
"if(ndc.x<-1.0||ndc.x>1.0||ndc.y<-1.0||ndc.y>1.0||ndc.z<0.0||ndc.z>1.0){return 1.0;}"
"let uv=vec2f(ndc.x*0.25+0.25+ox,0.5-ndc.y*0.5+oy);let bias=u.shadowParams.x+(1.0-ndl)*0.003;"
"let dims=vec2f(textureDimensions(shadowMap));let base=uv*dims;var bsum=0.0;var bc=0;"
"for(var y:i32=-2;y<=2;y+=1){for(var x:i32=-2;x<=2;x+=1){"
"let p=base+vec2f(f32(x),f32(y))*2.0;let d=textureLoad(shadowMap,vec2i(clamp(p,vec2f(0.0),dims-vec2f(1.0))),0);"
"if(d<ndc.z-bias){bsum+=d;bc+=1;}}}"
"if(bc==0){return 1.0;}"
"let pen=clamp((ndc.z-bsum/f32(bc))*u.shadowCfg.x*8.0,0.5,8.0);var sum=0.0;let texel=vec2f(u.shadowCfg.z,u.shadowCfg.w)*pen;"
"for(var y:i32=-2;y<=2;y+=1){for(var x:i32=-2;x<=2;x+=1){"
"sum+=textureSampleCompareLevel(shadowMap,shadowSamp,uv+vec2f(f32(x),f32(y))*texel,ndc.z-bias);}}"
"let sh=mix(1.0,sum/25.0,u.shadowParams.y);"
"let edge=smoothstep(sc*0.88,sc,dist);return mix(sh,1.0,edge*0.35);}"
"fn spotShadow(wp:vec3f)->f32{"
"if(u.spotParams.x<0.5){return 1.0;}"
"let c=u.spotVP*vec4f(wp,1.0);let ndc=c.xyz/c.w;"
"if(ndc.x<-1.0||ndc.x>1.0||ndc.y<-1.0||ndc.y>1.0||ndc.z<0.0||ndc.z>1.0){return 1.0;}"
"let uv=vec2f(ndc.x*0.5+0.5,0.5-ndc.y*0.5);let dims=vec2f(textureDimensions(spotMap));let base=uv*dims;"
"var bsum=0.0;var bc=0;for(var y:i32=-2;y<=2;y+=1){for(var x:i32=-2;x<=2;x+=1){"
"let p=base+vec2f(f32(x),f32(y))*2.0;let d=textureLoad(spotMap,vec2i(clamp(p,vec2f(0.0),dims-vec2f(1.0))),0);"
"if(d<ndc.z-u.spotParams.y){bsum+=d;bc+=1;}}}"
"if(bc==0){return 1.0;}"
"let pen=clamp((ndc.z-bsum/f32(bc))*u.shadowCfg.x*8.0,0.5,8.0);var sum=0.0;let tx=u.spotParams.z*pen;"
"for(var y:i32=-2;y<=2;y+=1){for(var x:i32=-2;x<=2;x+=1){"
"sum+=textureSampleCompareLevel(spotMap,shadowSamp,uv+vec2f(f32(x),f32(y))*tx,ndc.z-u.spotParams.y);}}"
"return mix(1.0,sum/25.0,u.spotParams.w);}"
"fn spotShadow2(wp:vec3f)->f32{"
"if(u.spot2Params.x<0.5){return 1.0;}"
"let c=u.spot2VP*vec4f(wp,1.0);let ndc=c.xyz/c.w;"
"if(ndc.x<-1.0||ndc.x>1.0||ndc.y<-1.0||ndc.y>1.0||ndc.z<0.0||ndc.z>1.0){return 1.0;}"
"let uv=vec2f(ndc.x*0.5+0.5,0.5-ndc.y*0.5);let dims=vec2f(textureDimensions(spotMap2));let base=uv*dims;"
"var bsum=0.0;var bc=0;for(var y:i32=-2;y<=2;y+=1){for(var x:i32=-2;x<=2;x+=1){"
"let p=base+vec2f(f32(x),f32(y))*2.0;let d=textureLoad(spotMap2,vec2i(clamp(p,vec2f(0.0),dims-vec2f(1.0))),0);"
"if(d<ndc.z-u.spot2Params.y){bsum+=d;bc+=1;}}}"
"if(bc==0){return 1.0;}"
"let pen=clamp((ndc.z-bsum/f32(bc))*u.shadowCfg.x*8.0,0.5,8.0);var sum=0.0;let tx=u.spot2Params.z*pen;"
"for(var y:i32=-2;y<=2;y+=1){for(var x:i32=-2;x<=2;x+=1){"
"sum+=textureSampleCompareLevel(spotMap2,shadowSamp,uv+vec2f(f32(x),f32(y))*tx,ndc.z-u.spot2Params.y);}}"
"return mix(1.0,sum/25.0,u.spot2Params.w);}"
"fn spotShadow3(wp:vec3f)->f32{"
"if(u.spot3Params.x<0.5){return 1.0;}"
"let c=u.spot3VP*vec4f(wp,1.0);let ndc=c.xyz/c.w;"
"if(ndc.x<-1.0||ndc.x>1.0||ndc.y<-1.0||ndc.y>1.0||ndc.z<0.0||ndc.z>1.0){return 1.0;}"
"let uv=vec2f(ndc.x*0.5+0.5,0.5-ndc.y*0.5);let dims=vec2f(textureDimensions(spotMap3));let base=uv*dims;"
"var bsum=0.0;var bc=0;for(var y:i32=-2;y<=2;y+=1){for(var x:i32=-2;x<=2;x+=1){"
"let p=base+vec2f(f32(x),f32(y))*2.0;let d=textureLoad(spotMap3,vec2i(clamp(p,vec2f(0.0),dims-vec2f(1.0))),0);"
"if(d<ndc.z-u.spot3Params.y){bsum+=d;bc+=1;}}}"
"if(bc==0){return 1.0;}"
"let pen=clamp((ndc.z-bsum/f32(bc))*u.shadowCfg.x*8.0,0.5,8.0);var sum=0.0;let tx=u.spot3Params.z*pen;"
"for(var y:i32=-2;y<=2;y+=1){for(var x:i32=-2;x<=2;x+=1){"
"sum+=textureSampleCompareLevel(spotMap3,shadowSamp,uv+vec2f(f32(x),f32(y))*tx,ndc.z-u.spot3Params.y);}}"
"return mix(1.0,sum/25.0,u.spot3Params.w);}"
"fn spotShadow4(wp:vec3f)->f32{"
"if(u.spot4Params.x<0.5){return 1.0;}"
"let c=u.spot4VP*vec4f(wp,1.0);let ndc=c.xyz/c.w;"
"if(ndc.x<-1.0||ndc.x>1.0||ndc.y<-1.0||ndc.y>1.0||ndc.z<0.0||ndc.z>1.0){return 1.0;}"
"let uv=vec2f(ndc.x*0.5+0.5,0.5-ndc.y*0.5);let dims=vec2f(textureDimensions(spotMap4));let base=uv*dims;"
"var bsum=0.0;var bc=0;for(var y:i32=-2;y<=2;y+=1){for(var x:i32=-2;x<=2;x+=1){"
"let p=base+vec2f(f32(x),f32(y))*2.0;let d=textureLoad(spotMap4,vec2i(clamp(p,vec2f(0.0),dims-vec2f(1.0))),0);"
"if(d<ndc.z-u.spot4Params.y){bsum+=d;bc+=1;}}}"
"if(bc==0){return 1.0;}"
"let pen=clamp((ndc.z-bsum/f32(bc))*u.shadowCfg.x*8.0,0.5,8.0);var sum=0.0;let tx=u.spot4Params.z*pen;"
"for(var y:i32=-2;y<=2;y+=1){for(var x:i32=-2;x<=2;x+=1){"
"sum+=textureSampleCompareLevel(spotMap4,shadowSamp,uv+vec2f(f32(x),f32(y))*tx,ndc.z-u.spot4Params.y);}}"
"return mix(1.0,sum/25.0,u.spot4Params.w);}"
"fn pointShadow(wp:vec3f)->f32{"
"if(u.pointShadowParams.x<0.5){return 1.0;}"
"let dir=normalize(wp-u.pointPos[0].xyz);let ax=abs(dir.x);let ay=abs(dir.y);let az=abs(dir.z);var c:vec4f;"
"if(ax>=ay&&ax>=az){if(dir.x>0.0){c=u.pointVP[0]*vec4f(wp,1.0);}else{c=u.pointVP[1]*vec4f(wp,1.0);}}"
"else if(ay>=ax&&ay>=az){if(dir.y>0.0){c=u.pointVP[2]*vec4f(wp,1.0);}else{c=u.pointVP[3]*vec4f(wp,1.0);}}"
"else{if(dir.z>0.0){c=u.pointVP[4]*vec4f(wp,1.0);}else{c=u.pointVP[5]*vec4f(wp,1.0);}}"
"let ndc=c.xyz/c.w;if(ndc.z<0.0||ndc.z>1.0){return 1.0;}"
"let bias=u.pointShadowParams.y;"
"var bsum=0.0;var bc=0;let bo=0.02*u.shadowCfg.x;"
"for(var yy:i32=-1;yy<=1;yy+=1){for(var xx:i32=-1;xx<=1;xx+=1){"
"let dd=textureSampleLevel(pointMapRaw,pointRawSamp,normalize(dir+vec3f(f32(xx)*bo,f32(yy)*bo,0.0)),0,0);"
"if(dd<ndc.z-bias){bsum+=dd;bc+=1;}}}"
"if(bc==0){return 1.0;}"
"let pen=clamp((ndc.z-bsum/f32(bc))*u.shadowCfg.x*8.0,0.5,8.0);var sum=0.0;let po=0.005*pen*u.shadowCfg.x;"
"for(var yy:i32=-1;yy<=1;yy+=1){for(var xx:i32=-1;xx<=1;xx+=1){"
"sum+=textureSampleCompareLevel(pointMap,shadowSamp,normalize(dir+vec3f(f32(xx)*po,f32(yy)*po,0.0)),0,ndc.z-bias);}}"
"return mix(1.0,sum/9.0,u.pointShadowParams.w);}"
"fn pointShadow2(wp:vec3f)->f32{"
"if(u.point2Params.x<0.5){return 1.0;}"
"let dir=normalize(wp-u.pointPos[1].xyz);let ax=abs(dir.x);let ay=abs(dir.y);let az=abs(dir.z);var c:vec4f;"
"if(ax>=ay&&ax>=az){if(dir.x>0.0){c=u.point2VP[0]*vec4f(wp,1.0);}else{c=u.point2VP[1]*vec4f(wp,1.0);}}"
"else if(ay>=ax&&ay>=az){if(dir.y>0.0){c=u.point2VP[2]*vec4f(wp,1.0);}else{c=u.point2VP[3]*vec4f(wp,1.0);}}"
"else{if(dir.z>0.0){c=u.point2VP[4]*vec4f(wp,1.0);}else{c=u.point2VP[5]*vec4f(wp,1.0);}}"
"let ndc=c.xyz/c.w;if(ndc.z<0.0||ndc.z>1.0){return 1.0;}let bias=u.point2Params.y;"
"var bsum=0.0;var bc=0;let bo=0.02*u.shadowCfg.x;"
"for(var yy:i32=-1;yy<=1;yy+=1){for(var xx:i32=-1;xx<=1;xx+=1){"
"let dd=textureSampleLevel(pointMapRaw,pointRawSamp,normalize(dir+vec3f(f32(xx)*bo,f32(yy)*bo,0.0)),1,0);"
"if(dd<ndc.z-bias){bsum+=dd;bc+=1;}}}"
"if(bc==0){return 1.0;}"
"let pen=clamp((ndc.z-bsum/f32(bc))*u.shadowCfg.x*8.0,0.5,8.0);var sum=0.0;let po=0.005*pen*u.shadowCfg.x;"
"for(var yy:i32=-1;yy<=1;yy+=1){for(var xx:i32=-1;xx<=1;xx+=1){"
"sum+=textureSampleCompareLevel(pointMap,shadowSamp,normalize(dir+vec3f(f32(xx)*po,f32(yy)*po,0.0)),1,ndc.z-bias);}}"
"return mix(1.0,sum/9.0,u.point2Params.w);}"
"fn pointShadow3(wp:vec3f)->f32{"
"if(u.point3Params.x<0.5){return 1.0;}"
"let dir=normalize(wp-u.pointPos[2].xyz);let ax=abs(dir.x);let ay=abs(dir.y);let az=abs(dir.z);var c:vec4f;"
"if(ax>=ay&&ax>=az){if(dir.x>0.0){c=u.point3VP[0]*vec4f(wp,1.0);}else{c=u.point3VP[1]*vec4f(wp,1.0);}}"
"else if(ay>=ax&&ay>=az){if(dir.y>0.0){c=u.point3VP[2]*vec4f(wp,1.0);}else{c=u.point3VP[3]*vec4f(wp,1.0);}}"
"else{if(dir.z>0.0){c=u.point3VP[4]*vec4f(wp,1.0);}else{c=u.point3VP[5]*vec4f(wp,1.0);}}"
"let ndc=c.xyz/c.w;if(ndc.z<0.0||ndc.z>1.0){return 1.0;}let bias=u.point3Params.y;"
"var bsum=0.0;var bc=0;let bo=0.02*u.shadowCfg.x;"
"for(var yy:i32=-1;yy<=1;yy+=1){for(var xx:i32=-1;xx<=1;xx+=1){"
"let dd=textureSampleLevel(pointMapRaw,pointRawSamp,normalize(dir+vec3f(f32(xx)*bo,f32(yy)*bo,0.0)),2,0);"
"if(dd<ndc.z-bias){bsum+=dd;bc+=1;}}}"
"if(bc==0){return 1.0;}"
"let pen=clamp((ndc.z-bsum/f32(bc))*u.shadowCfg.x*8.0,0.5,8.0);var sum=0.0;let po=0.005*pen*u.shadowCfg.x;"
"for(var yy:i32=-1;yy<=1;yy+=1){for(var xx:i32=-1;xx<=1;xx+=1){"
"sum+=textureSampleCompareLevel(pointMap,shadowSamp,normalize(dir+vec3f(f32(xx)*po,f32(yy)*po,0.0)),2,ndc.z-bias);}}"
"return mix(1.0,sum/9.0,u.point3Params.w);}"
"fn pointShadow4(wp:vec3f)->f32{"
"if(u.point4Params.x<0.5){return 1.0;}"
"let dir=normalize(wp-u.pointPos[3].xyz);let ax=abs(dir.x);let ay=abs(dir.y);let az=abs(dir.z);var c:vec4f;"
"if(ax>=ay&&ax>=az){if(dir.x>0.0){c=u.point4VP[0]*vec4f(wp,1.0);}else{c=u.point4VP[1]*vec4f(wp,1.0);}}"
"else if(ay>=ax&&ay>=az){if(dir.y>0.0){c=u.point4VP[2]*vec4f(wp,1.0);}else{c=u.point4VP[3]*vec4f(wp,1.0);}}"
"else{if(dir.z>0.0){c=u.point4VP[4]*vec4f(wp,1.0);}else{c=u.point4VP[5]*vec4f(wp,1.0);}}"
"let ndc=c.xyz/c.w;if(ndc.z<0.0||ndc.z>1.0){return 1.0;}let bias=u.point4Params.y;"
"var bsum=0.0;var bc=0;let bo=0.02*u.shadowCfg.x;"
"for(var yy:i32=-1;yy<=1;yy+=1){for(var xx:i32=-1;xx<=1;xx+=1){"
"let dd=textureSampleLevel(pointMapRaw,pointRawSamp,normalize(dir+vec3f(f32(xx)*bo,f32(yy)*bo,0.0)),3,0);"
"if(dd<ndc.z-bias){bsum+=dd;bc+=1;}}}"
"if(bc==0){return 1.0;}"
"let pen=clamp((ndc.z-bsum/f32(bc))*u.shadowCfg.x*8.0,0.5,8.0);var sum=0.0;let po=0.005*pen*u.shadowCfg.x;"
"for(var yy:i32=-1;yy<=1;yy+=1){for(var xx:i32=-1;xx<=1;xx+=1){"
"sum+=textureSampleCompareLevel(pointMap,shadowSamp,normalize(dir+vec3f(f32(xx)*po,f32(yy)*po,0.0)),3,ndc.z-bias);}}"
"return mix(1.0,sum/9.0,u.point4Params.w);}"
"@fragment fn fs(i:O)->@location(0)vec4f{"
"let A=textureSample(ta,s,i.uv);let M=textureSample(tm,s,i.uv);"
"var al=u.bc.rgb*A.rgb*i.c.rgb;let alpha=u.bc.a*A.a*i.c.a;"
"let met=clamp(u.mr.x*M.b,0,1);let rou=clamp(u.mr.y*M.g,0.04,1);"
"let nT=textureSample(normalTex,s,i.uv).rgb*2.0-1.0;var nz=nT.z;if(u.mr.z>0.5){nz=sqrt(max(1.0-nT.x*nT.x-nT.y*nT.y,0.0));}"
"let N0=normalize(i.n);"
"var Td=dpdx(i.w)*dpdy(i.uv).y-dpdy(i.w)*dpdx(i.uv).y;var Bd=dpdy(i.w)*dpdx(i.uv).x-dpdx(i.w)*dpdy(i.uv).x;"
"let hasT=i.t.w>0.5;"
"let T=select(Td,i.t.xyz,hasT);let B=select(Bd,cross(N0,normalize(i.t.xyz))*i.t.w,hasT);"
"let tl=max(length(T),1e-5);let bl=max(length(B),1e-5);"
"let N=normalize(T/tl*nT.x+B/bl*nT.y+N0*nz);"
"let ao=textureSample(aoTex,s,i.uv).r;let em=textureSample(emTex,s,i.uv).rgb;"
"let V=normalize(u.cam.xyz-i.w);let L=normalize(-u.ld.xyz);let H=normalize(V+L);"
"let ndl=max(dot(N,L),0);let ndv=max(dot(N,V),0);let ndh=max(dot(N,H),0);"
"let F0=mix(vec3f(0.04),al,met);let F=Fsch(max(dot(H,V),0),F0);"
"let a=rou*rou;let a2=a*a;let dnm=ndh*ndh*(a2-1)+1;let D=a2/(3.14159*dnm*dnm+1e-5);"
"let k=(rou+1)*(rou+1)/8;let G=(ndv/(ndv*(1-k)+k))*(ndl/(ndl*(1-k)+k));"
"let sp=(D*G*F)/(4*ndv*ndl+1e-5);let kd=(1-F)*(1-met);let dif=kd*al/3.14159;"
"let sh=shadowAt(i.w,ndl);"
"var col=(dif+sp)*u.lc.rgb*u.lc.a*ndl*sh+al*u.amb.rgb*u.amb.a*ao*(1.0-met);"
"if(u.ibl.x>0.0){"
"let irr=textureSampleLevel(irrTex,iblSamp,N,0.0).rgb;"
"let R=reflect(-V,N);"
"let mip=clamp(u.mr.y*(u.ibl.y-1.0),0.0,max(u.ibl.y-1.0,0.0));"
"let pre=textureSampleLevel(preTex,iblSamp,R,mip).rgb;"
"let ab=textureSampleLevel(brdfTex,iblSamp,vec2f(ndv,rou),0.0).rg;"
"let specOcc=clamp(pow(ndv+ao,exp2(-16.0*rou-1.0))-1.0+ao,0.0,1.0);"
"let specIbl=pre*(F0*ab.x+vec3f(ab.y))*specOcc;"
"let Fib=Fsch(ndv,F0);col += ((1.0-Fib)*(1.0-met)*al*irr+specIbl)*u.ibl.x*ao;}"
"if(u.spotParams.x>0.5){"
"let Ls=u.spotLight.xyz-i.w;let ds=length(Ls);let Lsn=Ls/max(ds,1e-4);"
"let sd=dot(-Lsn,u.spotDir.xyz);let cone=smoothstep(u.spotDir.w,u.spotDir.w+0.08,sd);"
"if(cone>0.0&&ds<u.spotLight.w){"
"let ndls=max(dot(N,Lsn),0);let Hsn=normalize(V+Lsn);let ndhs=max(dot(N,Hsn),0);"
"let Fs=Fsch(max(dot(Hsn,V),0),F0);let ar=rou*rou;let a2s=ar*ar;let dnms=ndhs*ndhs*(a2s-1)+1;"
"let Ds=a2s/(3.14159*dnms*dnms+1e-5);let ks=(rou+1)*(rou+1)/8;"
"let Gs=(ndv/(ndv*(1-ks)+ks))*(ndls/(ndls*(1-ks)+ks));"
"let sps=(Ds*Gs*Fs)/(4*ndv*ndls+1e-5);let kds=(1-Fs)*(1-met);let difs=kds*al/3.14159;"
"let att=1.0/(1.0+ds*ds/(u.spotLight.w*u.spotLight.w));"
"let svis=spotShadow(i.w);"
"col += (difs+sps)*u.spotColor.rgb*u.spotColor.w*cone*att*svis*ndls;"
"}}"
"for(var si:i32=0;si<4;si+=1){let sp=u.extraSpotPos[si];let sdir=u.extraSpotDir[si];let sc=u.extraSpotColor[si];"
"if(sp.w>0.0){let Lx=sp.xyz-i.w;let dx=length(Lx);let Lxn=Lx/max(dx,1e-4);"
"let sx=dot(-Lxn,sdir.xyz);let conx=smoothstep(sdir.w,sdir.w+0.08,sx);"
"if(conx>0.0&&dx<sp.w){let ndlx=max(dot(N,Lxn),0);let Hx=normalize(V+Lxn);let ndhx=max(dot(N,Hx),0);"
"let Fx=Fsch(max(dot(Hx,V),0),F0);let arx=rou*rou;let a2x=arx*arx;let dnmx=ndhx*ndhx*(a2x-1)+1;"
"let Dx=a2x/(3.14159*dnmx*dnmx+1e-5);let kx=(rou+1)*(rou+1)/8;"
"let Gx=(ndv/(ndv*(1-kx)+kx))*(ndlx/(ndlx*(1-kx)+kx));let spx=(Dx*Gx*Fx)/(4*ndv*ndlx+1e-5);"
"let kdx=(1-Fx)*(1-met);let difx=kdx*al/3.14159;let attx=1.0/(1.0+dx*dx/(sp.w*sp.w));"
"var sv2=1.0;if(si==0){sv2=spotShadow2(i.w);}else if(si==1){sv2=spotShadow3(i.w);}else if(si==2){sv2=spotShadow4(i.w);}col += (difx+spx)*sc.rgb*conx*attx*sv2*ndlx;}}}"
"for(var pi:i32=0;pi<4;pi+=1){"
"let pp=u.pointPos[pi];let pc=u.pointColor[pi];"
"if(pp.w>0.0){"
"let Lp=pp.xyz-i.w;let dp=length(Lp);let Lpn=Lp/max(dp,1e-4);let ndlpp=max(dot(N,Lpn),0);"
"let Hp=normalize(V+Lpn);let ndhp=max(dot(N,Hp),0);let Fp=Fsch(max(dot(Hp,V),0),F0);"
"let arp=rou*rou;let a2p=arp*arp;let dnmp=ndhp*ndhp*(a2p-1)+1;let Dp=a2p/(3.14159*dnmp*dnmp+1e-5);"
"let kp=(rou+1)*(rou+1)/8;let Gp=(ndv/(ndv*(1-kp)+kp))*(ndlpp/(ndlpp*(1-kp)+kp));"
"let spp=(Dp*Gp*Fp)/(4*ndv*ndlpp+1e-5);let kdp=(1-Fp)*(1-met);let difp=kdp*al/3.14159;"
"let attp=1.0/(1.0+dp*dp/(pp.w*pp.w));var pvis=1.0;if(pi==0){pvis=pointShadow(i.w);}else if(pi==1){pvis=pointShadow2(i.w);}else if(pi==2){pvis=pointShadow3(i.w);}else if(pi==3){pvis=pointShadow4(i.w);}"
"col += (difp+spp)*pc.rgb*pc.a*attp*pvis*ndlpp;"
"}}"
"col += em*u.bc.rgb;col*=u.exposure.x;"
"return vec4f(col,alpha);}";
static const char* SS=
"struct U{mvp:mat4x4f,};@group(0)@binding(0) var<uniform> u:U;"
"@vertex fn vs(@location(0)p:vec3f)->@builtin(position)vec4f{return u.mvp*vec4f(p,1);}";
static const char* SC=
"@group(0)@binding(0) var s:sampler;@group(0)@binding(1) var t:texture_2d<f32>;"
"struct O{@builtin(position)p:vec4f,@location(0)uv:vec2f,};"
"@vertex fn vs(@builtin(vertex_index)i:u32)->O{"
"var p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));"
"var o:O;o.p=vec4f(p[i],0,1);o.uv=(p[i]+vec2f(1))*vec2f(0.5,-0.5)+vec2f(0,1);return o;}"
"@fragment fn fs(i:O)->@location(0)vec4f{return textureSample(t,s,i.uv);}";
/* Display transform shared by the composite shaders: ACES tonemap + sRGB
   encode. The Vulkan PBR shader applies the same pair and writes to an sRGB
   swapchain; WebGPU canvases are unorm, so the composite encodes explicitly.
   Applied exactly once: postComposite writes g_finalTex, the final canvas blit
   uses the plain SC pipeline. */
#define VLCR_DISPLAY_WGSL \
"fn vlcrAces(x:vec3f)->vec3f{let a=2.51;let b=0.03;let c=2.43;let d=0.59;let e=0.14;" \
"return clamp((x*(a*x+b))/(x*(c*x+d)+e),vec3f(0.0),vec3f(1.0));}" \
"fn vlcrDisp(c:vec3f)->vec3f{let t=vlcrAces(max(c,vec3f(0.0)));" \
"let lo=t*12.92;let hi=vec3f(1.055)*pow(max(t,vec3f(0.0)),vec3f(1.0/2.4))-vec3f(0.055);" \
"return select(hi,lo,t<=vec3f(0.0031308));}"
static const char* SCD=
"@group(0)@binding(0) var s:sampler;@group(0)@binding(1) var t:texture_2d<f32>;"
"struct O{@builtin(position)p:vec4f,@location(0)uv:vec2f,};"
"@vertex fn vs(@builtin(vertex_index)i:u32)->O{"
"var p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));"
"var o:O;o.p=vec4f(p[i],0,1);o.uv=(p[i]+vec2f(1))*vec2f(0.5,-0.5)+vec2f(0,1);return o;}"
VLCR_DISPLAY_WGSL
"@fragment fn fs(i:O)->@location(0)vec4f{let c=textureSample(t,s,i.uv);return vec4f(vlcrDisp(c.rgb),c.a);}";
static const char* SF=
"@group(0)@binding(0) var s:sampler;@group(0)@binding(1) var t:texture_2d<f32>;"
"struct O{@builtin(position)p:vec4f,@location(0)uv:vec2f,};"
"@vertex fn vs(@builtin(vertex_index)i:u32)->O{"
"var p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));"
"var o:O;o.p=vec4f(p[i],0,1);o.uv=(p[i]+vec2f(1))*vec2f(0.5,-0.5)+vec2f(0,1);return o;}"
VLCR_DISPLAY_WGSL
"fn L(c:vec3f)->f32{return dot(c,vec3f(0.299,0.587,0.114));}"
"@fragment fn fs(i:O)->@location(0)vec4f{"
"let tx=1.0/vec2f(textureDimensions(t));"
"let nw=textureSample(t,s,i.uv+vec2f(-1,-1)*tx).rgb;"
"let ne=textureSample(t,s,i.uv+vec2f(1,-1)*tx).rgb;"
"let sw=textureSample(t,s,i.uv+vec2f(-1,1)*tx).rgb;"
"let se=textureSample(t,s,i.uv+vec2f(1,1)*tx).rgb;"
"let m=textureSample(t,s,i.uv).rgb;"
"let lnw=L(nw);let lne=L(ne);let lsw=L(sw);let lse=L(se);let lm=L(m);"
"let lmin=min(lm,min(min(lnw,lne),min(lsw,lse)));"
"let lmax=max(lm,max(max(lnw,lne),max(lsw,lse)));"
"var dir=vec2f(-((lnw+lne)-(lsw+lse)),((lnw+lsw)-(lne+lse)));"
"let reduce=max((lnw+lne+lsw+lse)*0.03125,0.0078125);"
"let rcp=1.0/(min(abs(dir.x),abs(dir.y))+reduce);"
"dir=clamp(dir*rcp,vec2f(-8.0),vec2f(8.0))*tx;"
"let a=0.5*(textureSample(t,s,i.uv+dir*(1.0/3.0-0.5)).rgb+textureSample(t,s,i.uv+dir*(2.0/3.0-0.5)).rgb);"
"let b=a*0.5+0.25*(textureSample(t,s,i.uv-dir*0.5).rgb+textureSample(t,s,i.uv+dir*0.5).rgb);"
"let lb=L(b);"
"if(lb<lmin||lb>lmax){return vec4f(vlcrDisp(a),1.0);}"
"return vec4f(vlcrDisp(b),1.0);}";
static const char* SB=
"@group(0)@binding(0) var s:sampler;@group(0)@binding(1) var t:texture_2d<f32>;"
"struct O{@builtin(position)p:vec4f,@location(0)uv:vec2f,};"
"@vertex fn vs(@builtin(vertex_index)i:u32)->O{"
"var p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));"
"var o:O;o.p=vec4f(p[i],0,1);o.uv=(p[i]+vec2f(1))*vec2f(0.5,-0.5)+vec2f(0,1);return o;}"
"@fragment fn fs(i:O)->@location(0)vec4f{"
"let tx=vec2f(1.0)/vec2f(textureDimensions(t));var acc=vec3f(0.0);"
"for(var y:i32=-1;y<=1;y+=1){for(var x:i32=-1;x<=1;x+=1){"
"let c=textureSample(t,s,i.uv+vec2f(f32(x),f32(y))*tx*1.5).rgb;"
"acc+=max(c-vec3f(0.65),vec3f(0.0));}}"
"return vec4f(acc/9.0,1.0);}";
static const char* SPB=
"@group(0)@binding(0) var s:sampler;@group(0)@binding(1) var scene:texture_2d<f32>;@group(0)@binding(2) var bloom:texture_2d<f32>;"
"struct O{@builtin(position)p:vec4f,@location(0)uv:vec2f,};"
"@vertex fn vs(@builtin(vertex_index)i:u32)->O{"
"var p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));"
"var o:O;o.p=vec4f(p[i],0,1);o.uv=(p[i]+vec2f(1))*vec2f(0.5,-0.5)+vec2f(0,1);return o;}"
VLCR_DISPLAY_WGSL
"@fragment fn fs(i:O)->@location(0)vec4f{"
"let base=textureSample(scene,s,i.uv).rgb;let glow=textureSample(bloom,s,i.uv).rgb;"
"return vec4f(vlcrDisp(base+glow*0.9),1.0);}";
static const char* SA=
"struct U{invProj:mat4x4f,params:vec4f,};@group(0)@binding(0) var<uniform> u:U;"
"@group(0)@binding(1) var depthTex:texture_depth_2d;"
"struct O{@builtin(position)p:vec4f,@location(0)uv:vec2f,};"
"@vertex fn vs(@builtin(vertex_index)i:u32)->O{"
"var p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));"
"var o:O;o.p=vec4f(p[i],0,1);o.uv=(p[i]+vec2f(1))*vec2f(0.5,-0.5)+vec2f(0,1);return o;}"
"@fragment fn fs(i:O)->@location(0)vec4f{"
"if(u.params.z<0.5){return vec4f(1.0);}"
"let dims=vec2f(textureDimensions(depthTex));let px=1.0/dims;"
"let c0=vec2i(clamp(i.uv,vec2f(0.0),vec2f(1.0))*dims);let d0=textureLoad(depthTex,c0,0);"
"if(d0>=1.0){return vec4f(1.0);}"
"var occ=0.0;let rad=u.params.x;"
"for(var k:i32=0;k<8;k+=1){"
"let ang=f32(k)*0.785398;let suv=clamp(i.uv+vec2f(cos(ang),sin(ang))*px*rad,vec2f(0.0),vec2f(1.0));"
"let ds=textureLoad(depthTex,vec2i(suv*dims),0);let diff=d0-ds;"
"if(diff>0.0005){occ+=clamp(1.0-diff/0.02,0.0,1.0);}"
"}"
"let ao=clamp(1.0-occ/8.0*u.params.y,0.0,1.0);return vec4f(ao,ao,ao,1.0);}";
static const char* SPA=
"@group(0)@binding(0) var s:sampler;@group(0)@binding(1) var scene:texture_2d<f32>;@group(0)@binding(2) var aoTex:texture_2d<f32>;"
"struct O{@builtin(position)p:vec4f,@location(0)uv:vec2f,};"
"@vertex fn vs(@builtin(vertex_index)i:u32)->O{"
"var p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));"
"var o:O;o.p=vec4f(p[i],0,1);o.uv=(p[i]+vec2f(1))*vec2f(0.5,-0.5)+vec2f(0,1);return o;}"
VLCR_DISPLAY_WGSL
"@fragment fn fs(i:O)->@location(0)vec4f{"
"let base=textureSample(scene,s,i.uv).rgb;let ao=textureSample(aoTex,s,i.uv).r;"
"return vec4f(vlcrDisp(base*mix(1.0,ao,0.8)),1.0);}";
static const char* ST=
"struct U{prevVP:mat4x4f,invVP:mat4x4f,params:vec4f,};@group(0)@binding(0) var<uniform> u:U;"
"@group(0)@binding(1) var scene:texture_2d<f32>;@group(0)@binding(2) var depthTex:texture_depth_2d;"
"@group(0)@binding(3) var history:texture_2d<f32>;@group(0)@binding(4) var s:sampler;"
"@group(0)@binding(5) var velTex:texture_2d<f32>;"
"struct O{@builtin(position)p:vec4f,@location(0)uv:vec2f,};"
"@vertex fn vs(@builtin(vertex_index)i:u32)->O{"
"var p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));"
"var o:O;o.p=vec4f(p[i],0,1);o.uv=(p[i]+vec2f(1))*vec2f(0.5,-0.5)+vec2f(0,1);return o;}"
"@fragment fn fs(i:O)->@location(0)vec4f{"
"let cur=textureSampleLevel(scene,s,i.uv,0.0).rgb;"
"if(u.params.z>0.5){return vec4f(cur,1.0);}"
"let dims=vec2f(textureDimensions(depthTex));"
"let vel=textureSampleLevel(velTex,s,i.uv,0.0).rg;"
"let puv=clamp(i.uv-vel,vec2f(0.0),vec2f(1.0));"
"let hist0=textureSampleLevel(history,s,puv,0.0).rgb;"
"let tx=1.0/dims;var mn=cur;var mx=cur;"
"for(var y:i32=-1;y<=1;y+=1){for(var x:i32=-1;x<=1;x+=1){"
"let c2=textureSampleLevel(scene,s,i.uv+vec2f(f32(x),f32(y))*tx,0.0).rgb;mn=min(mn,c2);mx=max(mx,c2);}}"
"let hist=clamp(hist0,mn,mx);return vec4f(mix(cur,hist,0.9),1.0);}";
static const char* SV=
"struct U{curVP:mat4x4f,prevVP:mat4x4f,curModel:mat4x4f,prevModel:mat4x4f,};"
"@group(0)@binding(0) var<uniform> u:U;"
"struct O{@builtin(position)p:vec4f,@location(0)v:vec2f,};"
"@vertex fn vs(@location(0)p:vec3f)->O{"
"let c=u.curVP*u.curModel*vec4f(p,1.0);let q=u.prevVP*u.prevModel*vec4f(p,1.0);"
"var o:O;o.p=c;o.v=(c.xy/c.w-q.xy/q.w)*0.5;return o;}"
"@fragment fn fs(i:O)->@location(0)vec4f{return vec4f(i.v,0.0,1.0);}";
/* Appended to the PBR source: bone palette + skinned vertex stage sharing the
   same fragment entry point. */
static const char* SKS=
"struct BB{cur:array<mat4x4f,256>,};"
"@group(3)@binding(0) var<uniform> bb:BB;"
"@vertex fn vss(@location(0)p:vec3f,@location(1)n:vec3f,@location(2)uv:vec2f,@location(4)j:vec4u,@location(5)w:vec4f,@location(6)t:vec4f)->O{"
"var o:O;let jj=min(j,vec4u(255u));var ww=w;let ws=w.x+w.y+w.z+w.w;"
"if(ws>1e-5){ww=w/ws;}else{ww=vec4f(1.0,0.0,0.0,0.0);}"
"let sk=bb.cur[jj.x]*ww.x+bb.cur[jj.y]*ww.y+bb.cur[jj.z]*ww.z+bb.cur[jj.w]*ww.w;"
"let lp=sk*vec4f(p,1.0);let wp=u.model*lp;"
"let c=u.mvp*lp;o.p=c;o.w=wp.xyz;o.n=normalize((u.model*(sk*vec4f(n,0.0))).xyz);o.uv=uv;o.c=vec4f(1.0);o.t=t;return o;}";
static const char* SSS=
"struct U{mvp:mat4x4f,};"
"@group(0)@binding(0) var<uniform> u:U;"
"struct BB{cur:array<mat4x4f,256>,};"
"@group(1)@binding(0) var<uniform> bb:BB;"
"@vertex fn vs(@location(0)p:vec3f,@location(4)j:vec4u,@location(5)w:vec4f)->@builtin(position)vec4f{"
"let jj=min(j,vec4u(255u));var ww=w;let ws=w.x+w.y+w.z+w.w;"
"if(ws>1e-5){ww=w/ws;}else{ww=vec4f(1.0,0.0,0.0,0.0);}"
"let sk=bb.cur[jj.x]*ww.x+bb.cur[jj.y]*ww.y+bb.cur[jj.z]*ww.z+bb.cur[jj.w]*ww.w;"
"return u.mvp*sk*vec4f(p,1.0);}";
static const char* SVS=
"struct U{curVP:mat4x4f,prevVP:mat4x4f,curModel:mat4x4f,prevModel:mat4x4f,};"
"@group(0)@binding(0) var<uniform> u:U;"
"struct BB{cur:array<mat4x4f,256>,prev:array<mat4x4f,256>,};"
"@group(1)@binding(0) var<uniform> bb:BB;"
"struct O{@builtin(position)p:vec4f,@location(0)v:vec2f,};"
"@vertex fn vs(@location(0)p:vec3f,@location(4)j:vec4u,@location(5)w:vec4f)->O{"
"let jj=min(j,vec4u(255u));"
"let cp=bb.cur[jj.x]*vec4f(p,1.0)*w.x+bb.cur[jj.y]*vec4f(p,1.0)*w.y+bb.cur[jj.z]*vec4f(p,1.0)*w.z+bb.cur[jj.w]*vec4f(p,1.0)*w.w;"
"let pp=bb.prev[jj.x]*vec4f(p,1.0)*w.x+bb.prev[jj.y]*vec4f(p,1.0)*w.y+bb.prev[jj.z]*vec4f(p,1.0)*w.z+bb.prev[jj.w]*vec4f(p,1.0)*w.w;"
"let c=u.curVP*u.curModel*cp;let q=u.prevVP*u.prevModel*pp;"
"var o:O;o.p=c;o.v=(c.xy/c.w-q.xy/q.w)*0.5;return o;}"
"@fragment fn fs(i:O)->@location(0)vec4f{return vec4f(i.v,0.0,1.0);}";
static const char* SL=
"struct U{mvp:mat4x4f,model:mat4x4f,ld:vec4f,lc:vec4f,amb:vec4f,cam:vec4f,bc:vec4f,mr:vec4f,};"
"@group(0)@binding(0) var<uniform> u:U;"
"struct O{@builtin(position)p:vec4f,@location(0)c:vec4f,};"
"@vertex fn vs(@location(0)p:vec3f,@location(1)n:vec3f,@location(2)uv:vec2f,@location(3)c:vec4f)->O{"
"var o:O;o.p=u.mvp*vec4f(p,1);o.c=c;return o;}@fragment fn fs(i:O)->@location(0)vec4f{return i.c;}";

static void cbd(WGPURequestDeviceStatus s,WGPUDevice d,char const*m,void*u){(void)u;if(s==WGPURequestDeviceStatus_Success){g_dev=d;return;}fprintf(stderr,"[webgpu] requestDevice failed status=%d: %s\n",(int)s,m?m:"(no message)");}
static void wgpuError(WGPUErrorType type,char const*message,void*user){(void)user;g_webgpuErrors++;fprintf(stderr,"[webgpu] error %d: %s\n",(int)type,message?message:"(no message)");}
int vgraphics_WebGPUErrorCount(void){return g_webgpuErrors;}
int vgraphics_GetValidationErrorCount(void){return g_webgpuErrors;}
static volatile int g_shotMapDone;static WGPUBufferMapAsyncStatus g_shotMapStatus;
static void shotMapped(WGPUBufferMapAsyncStatus s,void*u){(void)u;g_shotMapStatus=s;g_shotMapDone=1;}
static void tsMapped(WGPUBufferMapAsyncStatus s,void*u){int i=(int)(intptr_t)u;if(s==WGPUBufferMapAsyncStatus_Success)g_tsMapped[i]=1;}
static void cba(WGPURequestAdapterStatus s,WGPUAdapter a,char const*m,void*u){(void)u;if(s!=WGPURequestAdapterStatus_Success||!a){fprintf(stderr,"[webgpu] requestAdapter failed status=%d: %s\n",(int)s,m?m:"(no message)");return;}g_adapter=a;g_tsSupported=wgpuAdapterHasFeature(a,WGPUFeatureName_TimestampQuery)?1:0;
 g_bcSupported=wgpuAdapterHasFeature(a,WGPUFeatureName_TextureCompressionBC)?1:0;
 g_etc2Supported=wgpuAdapterHasFeature(a,WGPUFeatureName_TextureCompressionETC2)?1:0;
 g_astcSupported=wgpuAdapterHasFeature(a,WGPUFeatureName_TextureCompressionASTC)?1:0;
 g_f32Filterable=wgpuAdapterHasFeature(a,WGPUFeatureName_Float32Filterable)?1:0;
 WGPUDeviceDescriptor dd={0};int nf=0;
 if(g_tsSupported)g_reqFeatures[nf++]=WGPUFeatureName_TimestampQuery;
 if(g_bcSupported)g_reqFeatures[nf++]=WGPUFeatureName_TextureCompressionBC;
 if(g_etc2Supported)g_reqFeatures[nf++]=WGPUFeatureName_TextureCompressionETC2;
 if(g_astcSupported)g_reqFeatures[nf++]=WGPUFeatureName_TextureCompressionASTC;
 if(g_f32Filterable)g_reqFeatures[nf++]=WGPUFeatureName_Float32Filterable;
 dd.requiredFeatureCount=(size_t)nf;dd.requiredFeatures=nf?g_reqFeatures:NULL;
 wgpuAdapterRequestDevice(a,&dd,cbd,NULL);}
static void mcm(const Matrix*m,float o[16]){o[0]=m->m0;o[1]=m->m1;o[2]=m->m2;o[3]=m->m3;o[4]=m->m4;o[5]=m->m5;o[6]=m->m6;o[7]=m->m7;o[8]=m->m8;o[9]=m->m9;o[10]=m->m10;o[11]=m->m11;o[12]=m->m12;o[13]=m->m13;o[14]=m->m14;o[15]=m->m15;}
static WGPUShaderModule mk(const char*c){WGPUShaderModuleWGSLDescriptor w={0};w.chain.sType=WGPUSType_ShaderModuleWGSLDescriptor;w.code=c;WGPUShaderModuleDescriptor s={0};s.nextInChain=(WGPUChainedStruct*)&w;return wgpuDeviceCreateShaderModule(g_dev,&s);}

static void postGraphFree(void){
 if(g_bgBloom){wgpuBindGroupRelease(g_bgBloom);g_bgBloom=0;}
 if(g_bgSsaoComp){wgpuBindGroupRelease(g_bgSsaoComp);g_bgSsaoComp=0;}
 if(g_postGraph){vrgwDestroy(g_postGraph);g_postGraph=0;}
 g_bloomView=0;g_bloomTex=0;g_ssaoView=0;g_ssaoTex=0;
 g_pgScene=g_pgDepth=g_pgVel=g_pgTaa=g_pgFinal=g_pgSsao=g_pgBloom=-1;
}
static void mkdepth(void){
 if(g_bgSsao){wgpuBindGroupRelease(g_bgSsao);g_bgSsao=0;}
 if(g_depthV){wgpuTextureViewRelease(g_depthV);g_depthV=0;} if(g_depth){wgpuTextureRelease(g_depth);g_depth=0;}
 if(!g_dev)return; WGPUTextureDescriptor t={0};t.size.width=gw>0?gw:1;t.size.height=gh>0?gh:1;t.size.depthOrArrayLayers=1;
 t.mipLevelCount=1;t.sampleCount=1;t.dimension=WGPUTextureDimension_2D;t.format=WGPUTextureFormat_Depth24Plus;
 t.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding;
 g_depth=wgpuDeviceCreateTexture(g_dev,&t);g_depthV=wgpuTextureCreateView(g_depth,NULL);
 if(g_bgls&&g_ssaoUBO&&g_depthV){
  WGPUBindGroupEntry e[2]={{0}};e[0].binding=0;e[0].buffer=g_ssaoUBO;e[0].size=sizeof(SSAOUBO);
  e[1].binding=1;e[1].textureView=g_depthV;
  WGPUBindGroupDescriptor d={0};d.layout=g_bgls;d.entryCount=2;d.entries=e;
  g_bgSsao=wgpuDeviceCreateBindGroup(g_dev,&d);
 }
}
static void mkScene(void){
 postGraphFree();
 if(g_bgc){wgpuBindGroupRelease(g_bgc);g_bgc=0;}
 if(g_bgFinal){wgpuBindGroupRelease(g_bgFinal);g_bgFinal=0;}
 if(g_finalView){wgpuTextureViewRelease(g_finalView);g_finalView=0;}
 if(g_finalTex){wgpuTextureRelease(g_finalTex);g_finalTex=0;}
 if(g_sceneView){wgpuTextureViewRelease(g_sceneView);g_sceneView=0;}
 if(g_sceneTex){wgpuTextureRelease(g_sceneTex);g_sceneTex=0;}
 for(int i=0;i<2;++i){if(g_taaView[i])wgpuTextureViewRelease(g_taaView[i]);g_taaView[i]=0;
  if(g_taaTex[i])wgpuTextureRelease(g_taaTex[i]);g_taaTex[i]=0;}
 if(g_velView)wgpuTextureViewRelease(g_velView);g_velView=0;
 if(g_velTex)wgpuTextureRelease(g_velTex);g_velTex=0;
 g_taaCur=0;g_taaFrame=0;
 if(!g_dev)return;
 WGPUTextureDescriptor t={0};
 t.size.width=gw>0?gw:1;t.size.height=gh>0?gh:1;t.size.depthOrArrayLayers=1;
 t.mipLevelCount=1;t.sampleCount=1;t.dimension=WGPUTextureDimension_2D;t.format=g_fmt;
 t.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopySrc;
 g_sceneTex=wgpuDeviceCreateTexture(g_dev,&t);
 g_sceneView=wgpuTextureCreateView(g_sceneTex,NULL);
 WGPUTextureDescriptor ft=t;ft.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopySrc;
 g_finalTex=wgpuDeviceCreateTexture(g_dev,&ft);g_finalView=wgpuTextureCreateView(g_finalTex,NULL);
 if(g_bglc&&g_samp&&g_finalView){
  WGPUBindGroupEntry fe[2]={{0}};fe[0].binding=0;fe[0].sampler=g_samp;fe[1].binding=1;fe[1].textureView=g_finalView;
  WGPUBindGroupDescriptor fd={0};fd.layout=g_bglc;fd.entryCount=2;fd.entries=fe;
  g_bgFinal=wgpuDeviceCreateBindGroup(g_dev,&fd);
 }
 WGPUTextureDescriptor bt={0};bt.size.width=(uint32_t)(gw>1?gw/2:1);bt.size.height=(uint32_t)(gh>1?gh/2:1);
 bt.size.depthOrArrayLayers=1;bt.mipLevelCount=1;bt.sampleCount=1;bt.dimension=WGPUTextureDimension_2D;
 bt.format=g_fmt;
 for(int i=0;i<2;++i){g_taaTex[i]=wgpuDeviceCreateTexture(g_dev,&t);g_taaView[i]=wgpuTextureCreateView(g_taaTex[i],NULL);}
 t.format=WGPUTextureFormat_RGBA16Float;g_velTex=wgpuDeviceCreateTexture(g_dev,&t);
 g_velView=wgpuTextureCreateView(g_velTex,NULL);t.format=g_fmt;
 if(g_bglc&&g_samp&&g_sceneView){
  WGPUBindGroupEntry e[2]={0};
  e[0].binding=0;e[0].sampler=g_samp;
  e[1].binding=1;e[1].textureView=g_sceneView;
  WGPUBindGroupDescriptor d={0};d.layout=g_bglc;d.entryCount=2;d.entries=e;
  g_bgc=wgpuDeviceCreateBindGroup(g_dev,&d);
 }
}
static void mkPost(void){
 postGraphFree();
 if(!g_dev||!g_sceneTex||!g_sceneView||!g_finalTex||!g_finalView||!g_depthV)return;
 VrgwGraph*g=vrgwCreate();if(!g)return;
 g_pgScene=vrgwImportImage(g,"scene",g_sceneTex,g_sceneView,gw,gh,g_fmt,VRGW_USAGE_SAMPLED);
 g_pgDepth=vrgwImportImage(g,"depth",g_depth,g_depthV,gw,gh,WGPUTextureFormat_Depth24Plus,VRGW_USAGE_SAMPLED);
 g_pgVel=vrgwImportImage(g,"velocity",g_velTex,g_velView,gw,gh,WGPUTextureFormat_RGBA16Float,VRGW_USAGE_NONE);
 g_pgTaa=vrgwImportImage(g,"taa0",g_taaTex[0],g_taaView[0],gw,gh,g_fmt,VRGW_USAGE_NONE);
 int taa1=vrgwImportImage(g,"taa1",g_taaTex[1],g_taaView[1],gw,gh,g_fmt,VRGW_USAGE_NONE);
 g_pgFinal=vrgwImportImage(g,"final",g_finalTex,g_finalView,gw,gh,g_fmt,VRGW_USAGE_NONE);
 g_pgSsao=vrgwAddImage(g,"ssao",(uint32_t)(gw>1?gw/2:1),(uint32_t)(gh>1?gh/2:1),
                       WGPUTextureFormat_R8Unorm,WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding);
 g_pgBloom=vrgwAddImage(g,"bloom",(uint32_t)(gw>1?gw/2:1),(uint32_t)(gh>1?gh/2:1),
                        g_fmt,WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding);
 int ps=vrgwAddPass(g,"ssao"),pb=vrgwAddPass(g,"bloom"),pv=vrgwAddPass(g,"velocity"),
     pt=vrgwAddPass(g,"taa"),pc=vrgwAddPass(g,"composite");
 bool ok=ps==PG_SSAO&&pb==PG_BLOOM&&pv==PG_VELOCITY&&pt==PG_TAA&&pc==PG_COMPOSITE&&
         g_pgScene>=0&&g_pgDepth>=0&&g_pgVel>=0&&g_pgTaa>=0&&taa1>=0&&g_pgFinal>=0&&
         g_pgSsao>=0&&g_pgBloom>=0;
 ok=ok&&vrgwPassReadImage(g,ps,g_pgDepth,VRGW_USAGE_SAMPLED)&&
        vrgwPassWriteImage(g,ps,g_pgSsao,VRGW_USAGE_COLOR_ATTACHMENT)&&
        vrgwPassReadImage(g,pb,g_pgScene,VRGW_USAGE_SAMPLED)&&
        vrgwPassWriteImage(g,pb,g_pgBloom,VRGW_USAGE_COLOR_ATTACHMENT)&&
        vrgwPassWriteImage(g,pv,g_pgVel,VRGW_USAGE_COLOR_ATTACHMENT)&&
        vrgwPassReadImage(g,pt,g_pgScene,VRGW_USAGE_SAMPLED)&&
        vrgwPassReadImage(g,pt,g_pgDepth,VRGW_USAGE_SAMPLED)&&
        vrgwPassReadImage(g,pt,g_pgVel,VRGW_USAGE_SAMPLED)&&
        vrgwPassReadImage(g,pt,g_pgTaa,VRGW_USAGE_SAMPLED)&&
        vrgwPassReadImage(g,pt,taa1,VRGW_USAGE_SAMPLED)&&
        vrgwPassWriteImage(g,pt,g_pgTaa,VRGW_USAGE_COLOR_ATTACHMENT)&&
        vrgwPassWriteImage(g,pt,taa1,VRGW_USAGE_COLOR_ATTACHMENT)&&
        vrgwPassReadImage(g,pc,g_pgScene,VRGW_USAGE_SAMPLED)&&
        vrgwPassReadImage(g,pc,g_pgSsao,VRGW_USAGE_SAMPLED)&&
        vrgwPassReadImage(g,pc,g_pgBloom,VRGW_USAGE_SAMPLED)&&
        vrgwPassReadImage(g,pc,g_pgTaa,VRGW_USAGE_SAMPLED)&&
        vrgwPassReadImage(g,pc,taa1,VRGW_USAGE_SAMPLED)&&
        vrgwPassWriteImage(g,pc,g_pgFinal,VRGW_USAGE_COLOR_ATTACHMENT);
 if(!ok||!vrgwCompile(g,g_dev)){
  const char*e=vrgwGetError(g);
  fprintf(stderr,"[webgpu] post graph build failed: %s\n",e?e:"invalid image/pass handle");
  vrgwDestroy(g);return;
 }
 g_postGraph=g;
 g_bloomView=vrgwGetView(g,g_pgBloom);g_bloomTex=vrgwGetTexture(g,g_pgBloom);
 g_ssaoView=vrgwGetView(g,g_pgSsao);g_ssaoTex=vrgwGetTexture(g,g_pgSsao);
 if(g_bgls2&&g_samp&&g_sceneView&&g_ssaoView){
  WGPUBindGroupEntry se[3]={{0}};se[0].binding=0;se[0].sampler=g_samp;
  se[1].binding=1;se[1].textureView=g_sceneView;se[2].binding=2;se[2].textureView=g_ssaoView;
  WGPUBindGroupDescriptor sd={0};sd.layout=g_bgls2;sd.entryCount=3;sd.entries=se;
  g_bgSsaoComp=wgpuDeviceCreateBindGroup(g_dev,&sd);
 }
 if(g_bglp&&g_samp&&g_sceneView&&g_bloomView){
  WGPUBindGroupEntry be[3]={{0}};be[0].binding=0;be[0].sampler=g_samp;
  be[1].binding=1;be[1].textureView=g_sceneView;be[2].binding=2;be[2].textureView=g_bloomView;
  WGPUBindGroupDescriptor bd={0};bd.layout=g_bglp;bd.entryCount=3;bd.entries=be;
  g_bgBloom=wgpuDeviceCreateBindGroup(g_dev,&bd);
 }
}
static void cfg(void){
 if(!g_surf||!g_dev)return;
 if(g_swapchain){wgpuSwapChainRelease(g_swapchain);g_swapchain=0;}
 WGPUSwapChainDescriptor c={0};c.usage=WGPUTextureUsage_RenderAttachment;c.format=g_fmt;
 c.width=gw>0?gw:1;c.height=gh>0?gh:1;c.presentMode=WGPUPresentMode_Fifo;
 g_swapchain=wgpuDeviceCreateSwapChain(g_dev,g_surf,&c);mkScene();mkdepth();mkPost();
}
static WGPUTexture g_ntex,g_etex; static WGPUTextureView g_nview,g_eview;
static void oneTex(WGPUTexture*tex,WGPUTextureView*view,uint8_t r,uint8_t g,uint8_t b){
 uint8_t px[4]={r,g,b,255}; WGPUTextureDescriptor t={0};t.size.width=1;t.size.height=1;t.size.depthOrArrayLayers=1;
 t.mipLevelCount=1;t.sampleCount=1;t.dimension=WGPUTextureDimension_2D;t.format=WGPUTextureFormat_RGBA8Unorm;
 t.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;*tex=wgpuDeviceCreateTexture(g_dev,&t);*view=wgpuTextureCreateView(*tex,NULL);
 WGPUImageCopyTexture d={0};d.texture=*tex;WGPUTextureDataLayout l={0};l.bytesPerRow=4;l.rowsPerImage=1;WGPUExtent3D e={1,1,1};
 wgpuQueueWriteTexture(g_q,&d,px,4,&l,&e);
}
static void white(void){oneTex(&g_wtex,&g_wview,255,255,255);oneTex(&g_ntex,&g_nview,128,128,255);oneTex(&g_etex,&g_eview,0,0,0);}
static WGPUTextureView tview(unsigned id){if(id&&id<MT&&tex[id].u&&tex[id].v)return tex[id].v;return g_wview;}

static uint16_t f2h(float f){
 uint32_t x;memcpy(&x,&f,4);uint32_t sign=(x>>16)&0x8000u;int32_t e=(int32_t)((x>>23)&0xffu)-127+15;uint32_t m=x&0x7fffffu;
 if(e<=0){if(e<-10)return (uint16_t)sign;m=(m|0x800000u)>>(uint32_t)(1-e);return (uint16_t)(sign+(m>>13));}
 if(e>=31)return (uint16_t)(sign|0x7c00u|(m?0x200u:0));
 return (uint16_t)(sign|((uint32_t)e<<10)|(m>>13));
}
static WGPUTexture iblCubeTexture(int size,int mips){
 WGPUTextureDescriptor t={0};t.size.width=(uint32_t)size;t.size.height=(uint32_t)size;t.size.depthOrArrayLayers=6;
 t.mipLevelCount=(uint32_t)(mips>0?mips:1);t.sampleCount=1;t.dimension=WGPUTextureDimension_2D;
 t.format=g_f32Filterable?WGPUTextureFormat_RGBA32Float:WGPUTextureFormat_RGBA16Float;t.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;
 return wgpuDeviceCreateTexture(g_dev,&t);
}
static WGPUTextureView iblCubeView(WGPUTexture t,int mips){
 WGPUTextureViewDescriptor v={0};v.format=g_f32Filterable?WGPUTextureFormat_RGBA32Float:WGPUTextureFormat_RGBA16Float;v.dimension=WGPUTextureViewDimension_Cube;
 v.baseMipLevel=0;v.mipLevelCount=(uint32_t)(mips>0?mips:1);v.baseArrayLayer=0;v.arrayLayerCount=6;
 v.aspect=WGPUTextureAspect_All;return wgpuTextureCreateView(t,&v);
}
static WGPUTexture ibl2DTexture(int w,int h){
 WGPUTextureDescriptor t={0};t.size.width=(uint32_t)w;t.size.height=(uint32_t)h;t.size.depthOrArrayLayers=1;
 t.mipLevelCount=1;t.sampleCount=1;t.dimension=WGPUTextureDimension_2D;
 t.format=g_f32Filterable?WGPUTextureFormat_RGBA32Float:WGPUTextureFormat_RGBA16Float;t.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;
 return wgpuDeviceCreateTexture(g_dev,&t);
}
static WGPUTextureView ibl2DView(WGPUTexture t){
 WGPUTextureViewDescriptor v={0};v.format=g_f32Filterable?WGPUTextureFormat_RGBA32Float:WGPUTextureFormat_RGBA16Float;v.dimension=WGPUTextureViewDimension_2D;
 v.baseMipLevel=0;v.mipLevelCount=1;v.baseArrayLayer=0;v.arrayLayerCount=1;v.aspect=WGPUTextureAspect_All;
 return wgpuTextureCreateView(t,&v);
}
static void iblFallback(void){
 unsigned char zero[256]={0};
 if(g_f32Filterable){float zf[4]={0,0,0,1};memcpy(zero,zf,16);}
 else{uint16_t zf[4]={0,0,0,f2h(1.0f)};memcpy(zero,zf,8);}
 g_iblFallbackCubeTex=iblCubeTexture(1,1);
 g_iblFallbackCubeView=iblCubeView(g_iblFallbackCubeTex,1);
 WGPUImageCopyTexture d={0};d.texture=g_iblFallbackCubeTex;d.aspect=WGPUTextureAspect_All;
 WGPUTextureDataLayout l={0};l.bytesPerRow=256;l.rowsPerImage=1;WGPUExtent3D e={1,1,1};
 for(uint32_t f=0;f<6;++f){d.origin.z=f;wgpuQueueWriteTexture(g_q,&d,zero,256,&l,&e);}
 unsigned char lut[256]={0};
 if(g_f32Filterable){float lf[4]={1,0,0,1};memcpy(lut,lf,16);}
 else{uint16_t lf[4]={f2h(1.0f),0,0,f2h(1.0f)};memcpy(lut,lf,8);}
 g_iblFallback2DTex=ibl2DTexture(1,1);
 g_iblFallback2DView=ibl2DView(g_iblFallback2DTex);
 WGPUImageCopyTexture d2={0};d2.texture=g_iblFallback2DTex;d2.aspect=WGPUTextureAspect_All;
 wgpuQueueWriteTexture(g_q,&d2,lut,256,&l,&e);
}

static void pipes(void){
 WGPUSamplerDescriptor sd={0};sd.magFilter=WGPUFilterMode_Linear;sd.minFilter=WGPUFilterMode_Linear;
 sd.mipmapFilter=WGPUMipmapFilterMode_Linear;sd.addressModeU=WGPUAddressMode_Repeat;sd.addressModeV=WGPUAddressMode_Repeat;sd.maxAnisotropy=1;
 g_samp=wgpuDeviceCreateSampler(g_dev,&sd);white();
 /* 2d textured */
 {WGPUBindGroupLayoutEntry e2[2]={{0}};
  e2[0].binding=0;e2[0].visibility=WGPUShaderStage_Fragment;e2[0].sampler.type=WGPUSamplerBindingType_Filtering;
  e2[1].binding=1;e2[1].visibility=WGPUShaderStage_Fragment;e2[1].texture.sampleType=WGPUTextureSampleType_Float;e2[1].texture.viewDimension=WGPUTextureViewDimension_2D;
  WGPUBindGroupLayoutDescriptor bgl2d={0};bgl2d.entryCount=2;bgl2d.entries=e2;
  g_bgl2=wgpuDeviceCreateBindGroupLayout(g_dev,&bgl2d);
  WGPUPipelineLayoutDescriptor pld2={0};pld2.bindGroupLayoutCount=1;pld2.bindGroupLayouts=&g_bgl2;
  WGPUPipelineLayout pl2=wgpuDeviceCreatePipelineLayout(g_dev,&pld2);
  WGPUShaderModule m=mk(S2);
  WGPUVertexAttribute a[3]={{WGPUVertexFormat_Float32x2,0,0},{WGPUVertexFormat_Float32x2,8,1},{WGPUVertexFormat_Float32x4,16,2}};
  WGPUVertexBufferLayout v={0};v.arrayStride=sizeof(V2);v.attributeCount=3;v.attributes=a;
  WGPUBlendState bl={.color={WGPUBlendOperation_Add,WGPUBlendFactor_SrcAlpha,WGPUBlendFactor_OneMinusSrcAlpha},.alpha={WGPUBlendOperation_Add,WGPUBlendFactor_One,WGPUBlendFactor_OneMinusSrcAlpha}};
  WGPUColorTargetState ct={0};ct.format=g_fmt;ct.writeMask=WGPUColorWriteMask_All;ct.blend=&bl;
  WGPUFragmentState f={0};f.module=m;f.entryPoint="fs";f.targetCount=1;f.targets=&ct;
  WGPURenderPipelineDescriptor p={0};p.layout=pl2;p.vertex.module=m;p.vertex.entryPoint="vs";p.vertex.bufferCount=1;p.vertex.buffers=&v;p.fragment=&f;
  WGPUDepthStencilState ds2={0};ds2.format=WGPUTextureFormat_Depth24Plus;ds2.depthWriteEnabled=0;ds2.depthCompare=WGPUCompareFunction_Always;
  p.depthStencil=&ds2;
  p.primitive.topology=WGPUPrimitiveTopology_TriangleList;p.multisample.count=1;p.multisample.mask=~0u;
  g_p2=wgpuDeviceCreateRenderPipeline(g_dev,&p);wgpuShaderModuleRelease(m);wgpuPipelineLayoutRelease(pl2);
  WGPUBufferDescriptor b={0};b.usage=WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst;b.size=sizeof(b2);g_vb2=wgpuDeviceCreateBuffer(g_dev,&b);}
 /* pbr layout */
 {WGPUBindGroupLayoutEntry e[14]={{0}};
  e[0].binding=0;e[0].visibility=WGPUShaderStage_Vertex|WGPUShaderStage_Fragment;e[0].buffer.type=WGPUBufferBindingType_Uniform;e[0].buffer.hasDynamicOffset=1;e[0].buffer.minBindingSize=sizeof(UBO);
  e[1].binding=4;e[1].visibility=WGPUShaderStage_Fragment;e[1].texture.sampleType=WGPUTextureSampleType_Float;e[1].texture.viewDimension=WGPUTextureViewDimension_Cube;
  e[2].binding=5;e[2].visibility=WGPUShaderStage_Fragment;e[2].texture.sampleType=WGPUTextureSampleType_Float;e[2].texture.viewDimension=WGPUTextureViewDimension_Cube;
  e[3].binding=6;e[3].visibility=WGPUShaderStage_Fragment;e[3].texture.sampleType=WGPUTextureSampleType_Float;e[3].texture.viewDimension=WGPUTextureViewDimension_2D;
  e[4].binding=7;e[4].visibility=WGPUShaderStage_Fragment;e[4].sampler.type=WGPUSamplerBindingType_Filtering;
  e[5].binding=8;e[5].visibility=WGPUShaderStage_Fragment;e[5].texture.sampleType=WGPUTextureSampleType_Depth;e[5].texture.viewDimension=WGPUTextureViewDimension_2D;
  e[6].binding=9;e[6].visibility=WGPUShaderStage_Fragment;e[6].sampler.type=WGPUSamplerBindingType_Comparison;
  e[7].binding=10;e[7].visibility=WGPUShaderStage_Fragment;e[7].texture.sampleType=WGPUTextureSampleType_Depth;e[7].texture.viewDimension=WGPUTextureViewDimension_2D;
  e[8].binding=11;e[8].visibility=WGPUShaderStage_Fragment;e[8].texture.sampleType=WGPUTextureSampleType_Depth;e[8].texture.viewDimension=WGPUTextureViewDimension_CubeArray;
  e[9].binding=15;e[9].visibility=WGPUShaderStage_Fragment;e[9].texture.sampleType=WGPUTextureSampleType_Depth;e[9].texture.viewDimension=WGPUTextureViewDimension_CubeArray;
  e[10].binding=16;e[10].visibility=WGPUShaderStage_Fragment;e[10].sampler.type=WGPUSamplerBindingType_NonFiltering;
  e[11].binding=17;e[11].visibility=WGPUShaderStage_Fragment;e[11].texture.sampleType=WGPUTextureSampleType_Depth;e[11].texture.viewDimension=WGPUTextureViewDimension_2D;
  e[12].binding=18;e[12].visibility=WGPUShaderStage_Fragment;e[12].texture.sampleType=WGPUTextureSampleType_Depth;e[12].texture.viewDimension=WGPUTextureViewDimension_2D;
  e[13].binding=19;e[13].visibility=WGPUShaderStage_Fragment;e[13].texture.sampleType=WGPUTextureSampleType_Depth;e[13].texture.viewDimension=WGPUTextureViewDimension_2D;
  WGPUBindGroupLayoutDescriptor gd={0};gd.entryCount=13;gd.entries=e+1;g_bglGlob=wgpuDeviceCreateBindGroupLayout(g_dev,&gd);
  WGPUBindGroupLayoutDescriptor ud={0};ud.entryCount=1;ud.entries=e;g_bgl=wgpuDeviceCreateBindGroupLayout(g_dev,&ud);
  WGPUBindGroupLayoutEntry me[6]={{0}};
  me[0].binding=0;me[0].visibility=WGPUShaderStage_Fragment;me[0].sampler.type=WGPUSamplerBindingType_Filtering;
  for(int mi=1;mi<6;++mi){me[mi].binding=(uint32_t)mi;me[mi].visibility=WGPUShaderStage_Fragment;
   me[mi].texture.sampleType=WGPUTextureSampleType_Float;me[mi].texture.viewDimension=WGPUTextureViewDimension_2D;}
  WGPUBindGroupLayoutDescriptor md={0};md.entryCount=6;md.entries=me;g_bglMat=wgpuDeviceCreateBindGroupLayout(g_dev,&md);
  WGPUSamplerDescriptor isd={0};isd.magFilter=WGPUFilterMode_Linear;isd.minFilter=WGPUFilterMode_Linear;
  isd.mipmapFilter=WGPUMipmapFilterMode_Linear;isd.addressModeU=WGPUAddressMode_ClampToEdge;
  isd.addressModeV=WGPUAddressMode_ClampToEdge;isd.addressModeW=WGPUAddressMode_ClampToEdge;isd.maxAnisotropy=1;
  g_iblSamp=wgpuDeviceCreateSampler(g_dev,&isd);iblFallback();
  WGPUBufferDescriptor ub={0};ub.usage=WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst;ub.size=(uint64_t)MAX_DRAWS*UBO_STRIDE;g_ubo=wgpuDeviceCreateBuffer(g_dev,&ub);
  WGPUBindGroupLayout plays[3]={g_bgl,g_bglGlob,g_bglMat};
  WGPUPipelineLayoutDescriptor pld={0};pld.bindGroupLayoutCount=3;pld.bindGroupLayouts=plays;WGPUPipelineLayout pl=wgpuDeviceCreatePipelineLayout(g_dev,&pld);
  WGPUVertexAttribute a[5]={{WGPUVertexFormat_Float32x3,0,0},{WGPUVertexFormat_Float32x3,12,1},{WGPUVertexFormat_Float32x2,24,2},{WGPUVertexFormat_Float32x4,48,3},{WGPUVertexFormat_Float32x4,32,4}};
  WGPUVertexBufferLayout v={0};v.arrayStride=sizeof(V3);v.attributeCount=5;v.attributes=a;
  WGPUDepthStencilState ds={0};ds.format=WGPUTextureFormat_Depth24Plus;ds.depthWriteEnabled=1;ds.depthCompare=WGPUCompareFunction_Less;
  WGPUColorTargetState ct={0};ct.format=g_fmt;ct.writeMask=WGPUColorWriteMask_All;
  WGPUShaderModule m=mk(SP);WGPUFragmentState f={0};f.module=m;f.entryPoint="fs";f.targetCount=1;f.targets=&ct;
  WGPURenderPipelineDescriptor p={0};p.layout=pl;p.vertex.module=m;p.vertex.entryPoint="vs";p.vertex.bufferCount=1;p.vertex.buffers=&v;p.fragment=&f;
  p.primitive.topology=WGPUPrimitiveTopology_TriangleList;p.primitive.cullMode=WGPUCullMode_Back;p.primitive.frontFace=WGPUFrontFace_CCW;
  p.depthStencil=&ds;p.multisample.count=1;p.multisample.mask=~0u;g_pbr=wgpuDeviceCreateRenderPipeline(g_dev,&p);wgpuShaderModuleRelease(m);
  WGPUShaderModule ml=mk(SL);WGPUFragmentState fl={0};fl.module=ml;fl.entryPoint="fs";fl.targetCount=1;fl.targets=&ct;
  WGPUDepthStencilState dsl=ds;dsl.depthWriteEnabled=0;
  WGPUPipelineLayoutDescriptor pld1={0};pld1.bindGroupLayoutCount=1;pld1.bindGroupLayouts=&g_bgl;
  WGPUPipelineLayout pl1=wgpuDeviceCreatePipelineLayout(g_dev,&pld1);
  WGPURenderPipelineDescriptor pln={0};pln.layout=pl1;pln.vertex.module=ml;pln.vertex.entryPoint="vs";pln.vertex.bufferCount=1;pln.vertex.buffers=&v;pln.fragment=&fl;
  pln.primitive.topology=WGPUPrimitiveTopology_LineList;pln.depthStencil=&dsl;pln.multisample.count=1;pln.multisample.mask=~0u;
  g_plines=wgpuDeviceCreateRenderPipeline(g_dev,&pln);wgpuShaderModuleRelease(ml);wgpuPipelineLayoutRelease(pl1);wgpuPipelineLayoutRelease(pl);
  WGPUBufferDescriptor b={0};b.usage=WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst;b.size=(uint64_t)MAX_FRAME_VERTS*sizeof(V3);g_vb3=wgpuDeviceCreateBuffer(g_dev,&b);}
 /* Offscreen scene composite */
 {WGPUBindGroupLayoutEntry e[2]={{0}};
  e[0].binding=0;e[0].visibility=WGPUShaderStage_Fragment;e[0].sampler.type=WGPUSamplerBindingType_Filtering;
  e[1].binding=1;e[1].visibility=WGPUShaderStage_Fragment;e[1].texture.sampleType=WGPUTextureSampleType_Float;e[1].texture.viewDimension=WGPUTextureViewDimension_2D;
  WGPUBindGroupLayoutDescriptor bd={0};bd.entryCount=2;bd.entries=e;
  g_bglc=wgpuDeviceCreateBindGroupLayout(g_dev,&bd);
  WGPUPipelineLayoutDescriptor pld={0};pld.bindGroupLayoutCount=1;pld.bindGroupLayouts=&g_bglc;
  WGPUPipelineLayout pl=wgpuDeviceCreatePipelineLayout(g_dev,&pld);
  WGPUShaderModule m=mk(SCD);
  WGPUColorTargetState ct={0};ct.format=g_fmt;ct.writeMask=WGPUColorWriteMask_All;
  WGPUFragmentState f={0};f.module=m;f.entryPoint="fs";f.targetCount=1;f.targets=&ct;
  WGPURenderPipelineDescriptor p={0};p.layout=pl;p.vertex.module=m;p.vertex.entryPoint="vs";p.fragment=&f;
  p.primitive.topology=WGPUPrimitiveTopology_TriangleList;p.multisample.count=1;p.multisample.mask=~0u;
  g_pcomp=wgpuDeviceCreateRenderPipeline(g_dev,&p);
  WGPUShaderModule m2=mk(SF);WGPUFragmentState f2={0};f2.module=m2;f2.entryPoint="fs";f2.targetCount=1;f2.targets=&ct;
  p.vertex.module=m2;p.fragment=&f2;g_pcompFxaa=wgpuDeviceCreateRenderPipeline(g_dev,&p);wgpuShaderModuleRelease(m2);
  WGPUShaderModule m3=mk(SC);WGPUFragmentState f3={0};f3.module=m3;f3.entryPoint="fs";f3.targetCount=1;f3.targets=&ct;
  p.vertex.module=m3;p.fragment=&f3;g_pblit=wgpuDeviceCreateRenderPipeline(g_dev,&p);wgpuShaderModuleRelease(m3);
  wgpuShaderModuleRelease(m);wgpuPipelineLayoutRelease(pl);}
 /* bloom: bright/blur pass + additive composite */
 {WGPUBindGroupLayoutEntry pe[3]={{0}};
  pe[0].binding=0;pe[0].visibility=WGPUShaderStage_Fragment;pe[0].sampler.type=WGPUSamplerBindingType_Filtering;
  pe[1].binding=1;pe[1].visibility=WGPUShaderStage_Fragment;pe[1].texture.sampleType=WGPUTextureSampleType_Float;pe[1].texture.viewDimension=WGPUTextureViewDimension_2D;
  pe[2].binding=2;pe[2].visibility=WGPUShaderStage_Fragment;pe[2].texture.sampleType=WGPUTextureSampleType_Float;pe[2].texture.viewDimension=WGPUTextureViewDimension_2D;
  WGPUBindGroupLayoutDescriptor pbd={0};pbd.entryCount=3;pbd.entries=pe;g_bglp=wgpuDeviceCreateBindGroupLayout(g_dev,&pbd);
  WGPUPipelineLayoutDescriptor ppld={0};ppld.bindGroupLayoutCount=1;ppld.bindGroupLayouts=&g_bglp;
  WGPUPipelineLayout ppl=wgpuDeviceCreatePipelineLayout(g_dev,&ppld);
  WGPUColorTargetState pct={0};pct.format=g_fmt;pct.writeMask=WGPUColorWriteMask_All;
  WGPUShaderModule mb=mk(SB);WGPUFragmentState bf={0};bf.module=mb;bf.entryPoint="fs";bf.targetCount=1;bf.targets=&pct;
  WGPURenderPipelineDescriptor bp={0};bp.layout=(WGPUPipelineLayout)0;
  /* bright pass uses the 2-binding texture layout */
  WGPUPipelineLayoutDescriptor bpld={0};bpld.bindGroupLayoutCount=1;bpld.bindGroupLayouts=&g_bglc;
  WGPUPipelineLayout bpl=wgpuDeviceCreatePipelineLayout(g_dev,&bpld);
  bp.layout=bpl;bp.vertex.module=mb;bp.vertex.entryPoint="vs";bp.fragment=&bf;
  bp.primitive.topology=WGPUPrimitiveTopology_TriangleList;bp.multisample.count=1;bp.multisample.mask=~0u;
  g_pbright=wgpuDeviceCreateRenderPipeline(g_dev,&bp);wgpuShaderModuleRelease(mb);wgpuPipelineLayoutRelease(bpl);
  WGPUShaderModule mp=mk(SPB);WGPUFragmentState pf={0};pf.module=mp;pf.entryPoint="fs";pf.targetCount=1;pf.targets=&pct;
  WGPURenderPipelineDescriptor pp={0};pp.layout=ppl;pp.vertex.module=mp;pp.vertex.entryPoint="vs";pp.fragment=&pf;
  pp.primitive.topology=WGPUPrimitiveTopology_TriangleList;pp.multisample.count=1;pp.multisample.mask=~0u;
  g_pcompBloom=wgpuDeviceCreateRenderPipeline(g_dev,&pp);wgpuShaderModuleRelease(mp);wgpuPipelineLayoutRelease(ppl);
 }
 /* ssao: depth reconstruction + AO composite */
 {WGPUBindGroupLayoutEntry se[2]={{0}};
  se[0].binding=0;se[0].visibility=WGPUShaderStage_Fragment;se[0].buffer.type=WGPUBufferBindingType_Uniform;
  se[1].binding=1;se[1].visibility=WGPUShaderStage_Fragment;se[1].texture.sampleType=WGPUTextureSampleType_Depth;se[1].texture.viewDimension=WGPUTextureViewDimension_2D;
  WGPUBindGroupLayoutDescriptor sbd={0};sbd.entryCount=2;sbd.entries=se;g_bgls=wgpuDeviceCreateBindGroupLayout(g_dev,&sbd);
  WGPUPipelineLayoutDescriptor spld={0};spld.bindGroupLayoutCount=1;spld.bindGroupLayouts=&g_bgls;
  WGPUPipelineLayout spl=wgpuDeviceCreatePipelineLayout(g_dev,&spld);
  WGPUColorTargetState sct={0};sct.format=WGPUTextureFormat_R8Unorm;sct.writeMask=WGPUColorWriteMask_All;
  WGPUShaderModule sm=mk(SA);WGPUFragmentState sf={0};sf.module=sm;sf.entryPoint="fs";sf.targetCount=1;sf.targets=&sct;
  WGPURenderPipelineDescriptor sp={0};sp.layout=spl;sp.vertex.module=sm;sp.vertex.entryPoint="vs";sp.fragment=&sf;
  sp.primitive.topology=WGPUPrimitiveTopology_TriangleList;sp.multisample.count=1;sp.multisample.mask=~0u;
  g_pssao=wgpuDeviceCreateRenderPipeline(g_dev,&sp);wgpuShaderModuleRelease(sm);wgpuPipelineLayoutRelease(spl);
  WGPUBufferDescriptor sub={0};sub.usage=WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst;sub.size=256;g_ssaoUBO=wgpuDeviceCreateBuffer(g_dev,&sub);
  WGPUBindGroupLayoutEntry ce[3]={{0}};
  ce[0].binding=0;ce[0].visibility=WGPUShaderStage_Fragment;ce[0].sampler.type=WGPUSamplerBindingType_Filtering;
  ce[1].binding=1;ce[1].visibility=WGPUShaderStage_Fragment;ce[1].texture.sampleType=WGPUTextureSampleType_Float;ce[1].texture.viewDimension=WGPUTextureViewDimension_2D;
  ce[2].binding=2;ce[2].visibility=WGPUShaderStage_Fragment;ce[2].texture.sampleType=WGPUTextureSampleType_Float;ce[2].texture.viewDimension=WGPUTextureViewDimension_2D;
  WGPUBindGroupLayoutDescriptor cbd={0};cbd.entryCount=3;cbd.entries=ce;g_bgls2=wgpuDeviceCreateBindGroupLayout(g_dev,&cbd);
  WGPUPipelineLayoutDescriptor cpld={0};cpld.bindGroupLayoutCount=1;cpld.bindGroupLayouts=&g_bgls2;
  WGPUPipelineLayout cpl=wgpuDeviceCreatePipelineLayout(g_dev,&cpld);
  WGPUColorTargetState cct={0};cct.format=g_fmt;cct.writeMask=WGPUColorWriteMask_All;
  WGPUShaderModule cm=mk(SPA);WGPUFragmentState cf={0};cf.module=cm;cf.entryPoint="fs";cf.targetCount=1;cf.targets=&cct;
  WGPURenderPipelineDescriptor cp={0};cp.layout=cpl;cp.vertex.module=cm;cp.vertex.entryPoint="vs";cp.fragment=&cf;
  cp.primitive.topology=WGPUPrimitiveTopology_TriangleList;cp.multisample.count=1;cp.multisample.mask=~0u;
  g_pcompSsao=wgpuDeviceCreateRenderPipeline(g_dev,&cp);wgpuShaderModuleRelease(cm);wgpuPipelineLayoutRelease(cpl);
 }
 /* taa: jitter + history reprojection */
 {WGPUBindGroupLayoutEntry te[6]={{0}};
  te[0].binding=0;te[0].visibility=WGPUShaderStage_Fragment;te[0].buffer.type=WGPUBufferBindingType_Uniform;
  te[1].binding=1;te[1].visibility=WGPUShaderStage_Fragment;te[1].texture.sampleType=WGPUTextureSampleType_Float;te[1].texture.viewDimension=WGPUTextureViewDimension_2D;
  te[2].binding=2;te[2].visibility=WGPUShaderStage_Fragment;te[2].texture.sampleType=WGPUTextureSampleType_Depth;te[2].texture.viewDimension=WGPUTextureViewDimension_2D;
  te[3].binding=3;te[3].visibility=WGPUShaderStage_Fragment;te[3].texture.sampleType=WGPUTextureSampleType_Float;te[3].texture.viewDimension=WGPUTextureViewDimension_2D;
  te[4].binding=4;te[4].visibility=WGPUShaderStage_Fragment;te[4].sampler.type=WGPUSamplerBindingType_Filtering;
  te[5].binding=5;te[5].visibility=WGPUShaderStage_Fragment;te[5].texture.sampleType=WGPUTextureSampleType_Float;te[5].texture.viewDimension=WGPUTextureViewDimension_2D;
  WGPUBindGroupLayoutDescriptor tbd={0};tbd.entryCount=6;tbd.entries=te;g_bglTaa=wgpuDeviceCreateBindGroupLayout(g_dev,&tbd);
  WGPUPipelineLayoutDescriptor tpld={0};tpld.bindGroupLayoutCount=1;tpld.bindGroupLayouts=&g_bglTaa;
  WGPUPipelineLayout tpl=wgpuDeviceCreatePipelineLayout(g_dev,&tpld);
  WGPUColorTargetState tct={0};tct.format=g_fmt;tct.writeMask=WGPUColorWriteMask_All;
  WGPUShaderModule tm=mk(ST);WGPUFragmentState tf={0};tf.module=tm;tf.entryPoint="fs";tf.targetCount=1;tf.targets=&tct;
  WGPURenderPipelineDescriptor tp={0};tp.layout=tpl;tp.vertex.module=tm;tp.vertex.entryPoint="vs";tp.fragment=&tf;
  tp.primitive.topology=WGPUPrimitiveTopology_TriangleList;tp.multisample.count=1;tp.multisample.mask=~0u;
  g_pTaa=wgpuDeviceCreateRenderPipeline(g_dev,&tp);wgpuShaderModuleRelease(tm);wgpuPipelineLayoutRelease(tpl);
  WGPUBufferDescriptor tb={0};tb.usage=WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst;tb.size=256;g_taaUBO=wgpuDeviceCreateBuffer(g_dev,&tb);
 }
 /* velocity: previous/current clip-space motion */
 {WGPUBindGroupLayoutEntry ve={0};ve.binding=0;ve.visibility=WGPUShaderStage_Vertex;
  ve.buffer.type=WGPUBufferBindingType_Uniform;ve.buffer.hasDynamicOffset=1;ve.buffer.minBindingSize=sizeof(VELUBO);
  WGPUBindGroupLayoutDescriptor vbd={0};vbd.entryCount=1;vbd.entries=&ve;g_bglVel=wgpuDeviceCreateBindGroupLayout(g_dev,&vbd);
  WGPUPipelineLayoutDescriptor vpld={0};vpld.bindGroupLayoutCount=1;vpld.bindGroupLayouts=&g_bglVel;
  WGPUPipelineLayout vpl=wgpuDeviceCreatePipelineLayout(g_dev,&vpld);
  WGPUColorTargetState vct={0};vct.format=WGPUTextureFormat_RGBA16Float;vct.writeMask=WGPUColorWriteMask_All;
  WGPUShaderModule vm=mk(SV);WGPUFragmentState vf={0};vf.module=vm;vf.entryPoint="fs";vf.targetCount=1;vf.targets=&vct;
  WGPUVertexAttribute va={WGPUVertexFormat_Float32x3,0,0};
  WGPUVertexBufferLayout vvl={0};vvl.arrayStride=sizeof(V3);vvl.attributeCount=1;vvl.attributes=&va;
  WGPURenderPipelineDescriptor vp={0};vp.layout=vpl;vp.vertex.module=vm;vp.vertex.entryPoint="vs";
  vp.vertex.bufferCount=1;vp.vertex.buffers=&vvl;vp.fragment=&vf;
  vp.primitive.topology=WGPUPrimitiveTopology_TriangleList;vp.multisample.count=1;vp.multisample.mask=~0u;
  g_pVel=wgpuDeviceCreateRenderPipeline(g_dev,&vp);wgpuShaderModuleRelease(vm);wgpuPipelineLayoutRelease(vpl);
  WGPUBufferDescriptor vb={0};vb.usage=WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst;
  vb.size=(uint64_t)MAX_DRAWS*256;g_velUBO=wgpuDeviceCreateBuffer(g_dev,&vb);
  WGPUBufferDescriptor vvb={0};vvb.usage=WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst;
  vvb.size=(uint64_t)MAX_FRAME_VERTS*sizeof(V3);g_velVB=wgpuDeviceCreateBuffer(g_dev,&vvb);
 }
 /* skinning: bone palette layout, skinned PBR + skinned velocity pipelines */
 {WGPUBindGroupLayoutEntry be={0};be.binding=0;be.visibility=WGPUShaderStage_Vertex;
  be.buffer.type=WGPUBufferBindingType_Uniform;be.buffer.hasDynamicOffset=1;
  be.buffer.minBindingSize=VLCR_SKIN_BONE_BYTES;
  WGPUBindGroupLayoutDescriptor bd={0};bd.entryCount=1;bd.entries=&be;
  g_bglSkin=wgpuDeviceCreateBindGroupLayout(g_dev,&bd);
  WGPUBindGroupLayoutEntry ve2={0};ve2.binding=0;ve2.visibility=WGPUShaderStage_Vertex;
  ve2.buffer.type=WGPUBufferBindingType_Uniform;ve2.buffer.hasDynamicOffset=1;
  ve2.buffer.minBindingSize=VLCR_SKIN_CURPREV_BYTES;
  WGPUBindGroupLayoutDescriptor vd2={0};vd2.entryCount=1;vd2.entries=&ve2;
  g_bglSkinVel=wgpuDeviceCreateBindGroupLayout(g_dev,&vd2);
  WGPUBufferDescriptor ub={0};ub.usage=WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst;
  ub.size=(uint64_t)VLCR_SKIN_DRAWS*VLCR_SKIN_BONE_BYTES;g_skiUBO=wgpuDeviceCreateBuffer(g_dev,&ub);
  ub.size=(uint64_t)VLCR_SKIN_DRAWS*VLCR_SKIN_CURPREV_BYTES;g_velSkUBO=wgpuDeviceCreateBuffer(g_dev,&ub);
  WGPUBufferDescriptor svb={0};svb.usage=WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst;
  svb.size=(uint64_t)VLCR_SKIN_MAX_VERTS*sizeof(SV3);g_skiVB=wgpuDeviceCreateBuffer(g_dev,&svb);
  g_velSkVB=wgpuDeviceCreateBuffer(g_dev,&svb);
  WGPUBindGroupEntry be0={0};be0.binding=0;be0.buffer=g_skiUBO;be0.size=VLCR_SKIN_BONE_BYTES;
  WGPUBindGroupDescriptor bgd={0};bgd.layout=g_bglSkin;bgd.entryCount=1;bgd.entries=&be0;
  g_bgSkin=wgpuDeviceCreateBindGroup(g_dev,&bgd);
  be0.buffer=g_velSkUBO;be0.size=VLCR_SKIN_CURPREV_BYTES;
  bgd.layout=g_bglSkinVel;g_bgSkinVel=wgpuDeviceCreateBindGroup(g_dev,&bgd);
  WGPUVertexAttribute sa[6]={
   {WGPUVertexFormat_Float32x3,0,0},{WGPUVertexFormat_Float32x3,12,1},
   {WGPUVertexFormat_Float32x2,24,2},{WGPUVertexFormat_Uint32x4,48,4},
   {WGPUVertexFormat_Float32x4,64,5},{WGPUVertexFormat_Float32x4,32,6}};
  WGPUVertexBufferLayout svl={0};svl.arrayStride=sizeof(SV3);svl.attributeCount=6;svl.attributes=sa;
  size_t slen=strlen(SP)+strlen(SKS)+1;
  char* ssrc=(char*)malloc(slen);
  if(ssrc){snprintf(ssrc,slen,"%s%s",SP,SKS);g_skinMod=mk(ssrc);free(ssrc);}
  WGPUBindGroupLayout lays[4]={g_bgl,g_bglGlob,g_bglMat,g_bglSkin};
  WGPUPipelineLayoutDescriptor spd={0};spd.bindGroupLayoutCount=4;spd.bindGroupLayouts=lays;
  WGPUPipelineLayout spl=wgpuDeviceCreatePipelineLayout(g_dev,&spd);
  if(g_skinMod){
   WGPUDepthStencilState ds={0};ds.format=WGPUTextureFormat_Depth24Plus;ds.depthWriteEnabled=1;ds.depthCompare=WGPUCompareFunction_Less;
   WGPUColorTargetState ct={0};ct.format=g_fmt;ct.writeMask=WGPUColorWriteMask_All;
   WGPUFragmentState f={0};f.module=g_skinMod;f.entryPoint="fs";f.targetCount=1;f.targets=&ct;
   WGPURenderPipelineDescriptor p={0};p.layout=spl;p.vertex.module=g_skinMod;p.vertex.entryPoint="vss";
   p.vertex.bufferCount=1;p.vertex.buffers=&svl;p.fragment=&f;
   p.primitive.topology=WGPUPrimitiveTopology_TriangleList;p.primitive.cullMode=WGPUCullMode_None;
   p.primitive.frontFace=WGPUFrontFace_CCW;p.depthStencil=&ds;p.multisample.count=1;p.multisample.mask=~0u;
   g_pskin=wgpuDeviceCreateRenderPipeline(g_dev,&p);
   WGPUBindGroupLayout lays2[2]={g_bglVel,g_bglSkinVel};
   WGPUPipelineLayoutDescriptor vpd={0};vpd.bindGroupLayoutCount=2;vpd.bindGroupLayouts=lays2;
   WGPUPipelineLayout vpl=wgpuDeviceCreatePipelineLayout(g_dev,&vpd);
   WGPUShaderModule vm=mk(SVS);
   WGPUVertexAttribute vaa[3]={{WGPUVertexFormat_Float32x3,0,0},{WGPUVertexFormat_Uint32x4,48,4},{WGPUVertexFormat_Float32x4,64,5}};
   WGPUVertexBufferLayout vvl={0};vvl.arrayStride=sizeof(SV3);vvl.attributeCount=3;vvl.attributes=vaa;
   WGPUColorTargetState vct={0};vct.format=WGPUTextureFormat_RGBA16Float;vct.writeMask=WGPUColorWriteMask_All;
   WGPUFragmentState vf={0};vf.module=vm;vf.entryPoint="fs";vf.targetCount=1;vf.targets=&vct;
   WGPURenderPipelineDescriptor vp={0};vp.layout=vpl;vp.vertex.module=vm;vp.vertex.entryPoint="vs";
   vp.vertex.bufferCount=1;vp.vertex.buffers=&vvl;vp.fragment=&vf;
   vp.primitive.topology=WGPUPrimitiveTopology_TriangleList;vp.multisample.count=1;vp.multisample.mask=~0u;
   g_pVelSkin=wgpuDeviceCreateRenderPipeline(g_dev,&vp);wgpuShaderModuleRelease(vm);wgpuPipelineLayoutRelease(vpl);
  }
  wgpuPipelineLayoutRelease(spl);
 }
}

static WGPUBindGroup mkbgShadow(WGPUBuffer buffer){
 WGPUBindGroupEntry e={0};e.binding=0;e.buffer=buffer;e.size=sizeof(UBO);
 WGPUBindGroupDescriptor d={0};d.layout=g_bglShadow;d.entryCount=1;d.entries=&e;
 return wgpuDeviceCreateBindGroup(g_dev,&d);
}
static void initShadow(void){
 WGPUTextureDescriptor st={0};st.size.width=2048;st.size.height=2048;st.size.depthOrArrayLayers=1;
 st.mipLevelCount=1;st.sampleCount=1;st.dimension=WGPUTextureDimension_2D;
 st.format=WGPUTextureFormat_Depth32Float;st.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding;
 g_shadowTex=wgpuDeviceCreateTexture(g_dev,&st);
 WGPUTextureViewDescriptor sv={0};sv.format=WGPUTextureFormat_Depth32Float;sv.dimension=WGPUTextureViewDimension_2D;
 sv.baseMipLevel=0;sv.mipLevelCount=1;sv.baseArrayLayer=0;sv.arrayLayerCount=1;sv.aspect=WGPUTextureAspect_DepthOnly;
 g_shadowView=wgpuTextureCreateView(g_shadowTex,&sv);
 WGPUTextureDescriptor spt=st;spt.size.width=1024;spt.size.height=1024;g_spotTex=wgpuDeviceCreateTexture(g_dev,&spt);
 WGPUTextureViewDescriptor svp=sv;g_spotView=wgpuTextureCreateView(g_spotTex,&svp);
 g_spot2Tex=wgpuDeviceCreateTexture(g_dev,&spt);g_spot2View=wgpuTextureCreateView(g_spot2Tex,&svp);
 g_spot3Tex=wgpuDeviceCreateTexture(g_dev,&spt);g_spot3View=wgpuTextureCreateView(g_spot3Tex,&svp);
 g_spot4Tex=wgpuDeviceCreateTexture(g_dev,&spt);g_spot4View=wgpuTextureCreateView(g_spot4Tex,&svp);
 WGPUTextureDescriptor ptd=st;ptd.size.width=512;ptd.size.height=512;ptd.size.depthOrArrayLayers=6;
 g_pointTex=wgpuDeviceCreateTexture(g_dev,&ptd);
 WGPUTextureViewDescriptor pvd=sv;pvd.dimension=WGPUTextureViewDimension_Cube;pvd.arrayLayerCount=6;
 g_pointView=wgpuTextureCreateView(g_pointTex,&pvd);
 g_point2Tex=wgpuDeviceCreateTexture(g_dev,&ptd);g_point2View=wgpuTextureCreateView(g_point2Tex,&pvd);
 g_point3Tex=wgpuDeviceCreateTexture(g_dev,&ptd);g_point3View=wgpuTextureCreateView(g_point3Tex,&pvd);
 WGPUTextureDescriptor pad=ptd;pad.size.depthOrArrayLayers=24;
 g_pointArrTex=wgpuDeviceCreateTexture(g_dev,&pad);
 WGPUTextureViewDescriptor pav=sv;pav.dimension=WGPUTextureViewDimension_CubeArray;pav.arrayLayerCount=24;
 g_pointArrView=wgpuTextureCreateView(g_pointArrTex,&pav);
 for(int f=0;f<6;++f){WGPUTextureViewDescriptor fvd=sv;fvd.dimension=WGPUTextureViewDimension_2D;fvd.baseArrayLayer=(uint32_t)f;fvd.arrayLayerCount=1;
  g_pointFaceView[f]=wgpuTextureCreateView(g_pointTex,&fvd);
  g_point2FaceView[f]=wgpuTextureCreateView(g_point2Tex,&fvd);
  g_point3FaceView[f]=wgpuTextureCreateView(g_point3Tex,&fvd);}
 for(int p=0;p<4;++p)for(int f=0;f<6;++f){WGPUTextureViewDescriptor fvd=sv;fvd.dimension=WGPUTextureViewDimension_2D;
  fvd.baseArrayLayer=(uint32_t)(p*6+f);fvd.arrayLayerCount=1;g_pointArrFace[p][f]=wgpuTextureCreateView(g_pointArrTex,&fvd);}
 WGPUSamplerDescriptor ssd={0};ssd.compare=WGPUCompareFunction_Less;ssd.magFilter=WGPUFilterMode_Linear;
 ssd.minFilter=WGPUFilterMode_Linear;ssd.mipmapFilter=WGPUMipmapFilterMode_Nearest;
 ssd.addressModeU=WGPUAddressMode_ClampToEdge;ssd.addressModeV=WGPUAddressMode_ClampToEdge;
 ssd.addressModeW=WGPUAddressMode_ClampToEdge;ssd.maxAnisotropy=1;
 g_shadowSamp=wgpuDeviceCreateSampler(g_dev,&ssd);
 WGPUSamplerDescriptor prs={0};prs.magFilter=WGPUFilterMode_Nearest;prs.minFilter=WGPUFilterMode_Nearest;
 prs.mipmapFilter=WGPUMipmapFilterMode_Nearest;prs.addressModeU=WGPUAddressMode_ClampToEdge;
 prs.addressModeV=WGPUAddressMode_ClampToEdge;prs.addressModeW=WGPUAddressMode_ClampToEdge;prs.maxAnisotropy=1;
 g_pointRawSamp=wgpuDeviceCreateSampler(g_dev,&prs);
 WGPUBindGroupLayoutEntry se={0};se.binding=0;se.visibility=WGPUShaderStage_Vertex;
 se.buffer.type=WGPUBufferBindingType_Uniform;se.buffer.hasDynamicOffset=1;se.buffer.minBindingSize=sizeof(UBO);
 WGPUBindGroupLayoutDescriptor sbd={0};sbd.entryCount=1;sbd.entries=&se;
 g_bglShadow=wgpuDeviceCreateBindGroupLayout(g_dev,&sbd);
 WGPUBufferDescriptor sub={0};sub.usage=WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst;
 sub.size=(uint64_t)MAX_DRAWS*UBO_STRIDE;g_shadowUBO=wgpuDeviceCreateBuffer(g_dev,&sub);
 WGPUBufferDescriptor svb={0};svb.usage=WGPUBufferUsage_Vertex|WGPUBufferUsage_CopyDst;
 svb.size=(uint64_t)MAX_FRAME_VERTS*sizeof(V3);g_shadowVB=wgpuDeviceCreateBuffer(g_dev,&svb);
 g_shadowUBO2=wgpuDeviceCreateBuffer(g_dev,&sub);g_shadowVB2=wgpuDeviceCreateBuffer(g_dev,&svb);
 g_shadowUBO3=wgpuDeviceCreateBuffer(g_dev,&sub);g_shadowVB3=wgpuDeviceCreateBuffer(g_dev,&svb);
 g_shadowUBO4=wgpuDeviceCreateBuffer(g_dev,&sub);g_shadowVB4=wgpuDeviceCreateBuffer(g_dev,&svb);
 g_spotUBO=wgpuDeviceCreateBuffer(g_dev,&sub);g_spotVB=wgpuDeviceCreateBuffer(g_dev,&svb);
 g_spot2UBO=wgpuDeviceCreateBuffer(g_dev,&sub);g_spot2VB=wgpuDeviceCreateBuffer(g_dev,&svb);
 g_spot3UBO=wgpuDeviceCreateBuffer(g_dev,&sub);g_spot3VB=wgpuDeviceCreateBuffer(g_dev,&svb);
 g_spot4UBO=wgpuDeviceCreateBuffer(g_dev,&sub);g_spot4VB=wgpuDeviceCreateBuffer(g_dev,&svb);
 for(int f=0;f<6;++f){g_pointUBO[f]=wgpuDeviceCreateBuffer(g_dev,&sub);g_pointVB[f]=wgpuDeviceCreateBuffer(g_dev,&svb);
  g_point2UBO[f]=wgpuDeviceCreateBuffer(g_dev,&sub);g_point2VB[f]=wgpuDeviceCreateBuffer(g_dev,&svb);
  g_point3UBO[f]=wgpuDeviceCreateBuffer(g_dev,&sub);g_point3VB[f]=wgpuDeviceCreateBuffer(g_dev,&svb);}
 WGPUPipelineLayoutDescriptor pld={0};pld.bindGroupLayoutCount=1;pld.bindGroupLayouts=&g_bglShadow;
 WGPUPipelineLayout pl=wgpuDeviceCreatePipelineLayout(g_dev,&pld);
 WGPUShaderModule m=mk(SS);
 WGPUVertexAttribute a={WGPUVertexFormat_Float32x3,0,0};
 WGPUVertexBufferLayout v={0};v.arrayStride=sizeof(V3);v.attributeCount=1;v.attributes=&a;
 WGPUDepthStencilState ds={0};ds.format=WGPUTextureFormat_Depth32Float;ds.depthWriteEnabled=1;ds.depthCompare=WGPUCompareFunction_Less;
 WGPURenderPipelineDescriptor p={0};p.layout=pl;p.vertex.module=m;p.vertex.entryPoint="vs";
 p.vertex.bufferCount=1;p.vertex.buffers=&v;p.primitive.topology=WGPUPrimitiveTopology_TriangleList;
 p.primitive.cullMode=WGPUCullMode_None;p.depthStencil=&ds;p.multisample.count=1;p.multisample.mask=~0u;
 g_pShadow=wgpuDeviceCreateRenderPipeline(g_dev,&p);wgpuShaderModuleRelease(m);wgpuPipelineLayoutRelease(pl);
 if(g_bglSkin){
  WGPUBindGroupLayout slays[2]={g_bglShadow,g_bglSkin};
  WGPUPipelineLayoutDescriptor spld={0};spld.bindGroupLayoutCount=2;spld.bindGroupLayouts=slays;
  WGPUPipelineLayout spl=wgpuDeviceCreatePipelineLayout(g_dev,&spld);
  WGPUShaderModule sm=mk(SSS);
  WGPUVertexAttribute sa[3]={{WGPUVertexFormat_Float32x3,0,0},{WGPUVertexFormat_Uint32x4,48,4},{WGPUVertexFormat_Float32x4,64,5}};
  WGPUVertexBufferLayout svl={0};svl.arrayStride=sizeof(SV3);svl.attributeCount=3;svl.attributes=sa;
  WGPURenderPipelineDescriptor sp={0};sp.layout=spl;sp.vertex.module=sm;sp.vertex.entryPoint="vs";
  sp.vertex.bufferCount=1;sp.vertex.buffers=&svl;sp.primitive.topology=WGPUPrimitiveTopology_TriangleList;
  sp.primitive.cullMode=WGPUCullMode_None;sp.depthStencil=&ds;sp.multisample.count=1;sp.multisample.mask=~0u;
  g_pshadowSkin=wgpuDeviceCreateRenderPipeline(g_dev,&sp);wgpuShaderModuleRelease(sm);wgpuPipelineLayoutRelease(spl);
 }
 g_shadowVP=MatrixIdentity();g_shadowTexel=1.0f/1024.0f;
}
VkInitResult InitGraphicsDevice(void){
 g_inst=wgpuCreateInstance(NULL);if(!g_inst)return VKINIT_ERR_INSTANCE;
 wgpuInstanceRequestAdapter(g_inst,NULL,cba,NULL);int s=0;while(!g_dev&&s++<5000)emscripten_sleep(1);
 if(!g_dev){fprintf(stderr,"[webgpu] requestDevice timed out after 5s\n");return VKINIT_ERR_DEVICE;}fprintf(stderr,"[webgpu] features timestamp=%d bc=%d etc2=%d astc=%d f32f=%d\n",g_tsSupported,g_bcSupported,g_etc2Supported,g_astcSupported,g_f32Filterable);g_webgpuErrors=0;wgpuDeviceSetUncapturedErrorCallback(g_dev,wgpuError,NULL);g_q=wgpuDeviceGetQueue(g_dev);
 if(g_tsSupported){WGPUQuerySetDescriptor q={0};q.type=WGPUQueryType_Timestamp;q.count=2;g_tsSet=wgpuDeviceCreateQuerySet(g_dev,&q);
  WGPUBufferDescriptor tb={0};tb.usage=WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst;tb.size=16;
  g_tsBuf[0]=wgpuDeviceCreateBuffer(g_dev,&tb);g_tsBuf[1]=wgpuDeviceCreateBuffer(g_dev,&tb);
  WGPUBufferDescriptor tr={0};tr.usage=WGPUBufferUsage_QueryResolve|WGPUBufferUsage_CopySrc;tr.size=16;
  g_tsResolve[0]=wgpuDeviceCreateBuffer(g_dev,&tr);g_tsResolve[1]=wgpuDeviceCreateBuffer(g_dev,&tr);}
 WGPUSurfaceDescriptorFromCanvasHTMLSelector c={0};c.chain.sType=WGPUSType_SurfaceDescriptorFromCanvasHTMLSelector;c.selector="#canvas";
 WGPUSurfaceDescriptor sd={0};sd.nextInChain=(WGPUChainedStruct*)&c;g_surf=wgpuInstanceCreateSurface(g_inst,&sd);
 if(!g_surf)return VKINIT_ERR_SURFACE;
 if(g_adapter)g_fmt=wgpuSurfaceGetPreferredFormat(g_surf,g_adapter);
 g_view=MatrixIdentity();g_proj=MatrixIdentity();cfg();pipes();initShadow();mkScene();mkdepth();mkPost();g_ok=1;
 fprintf(stderr,"[vlcrlib] WebGPU PBR+glTF\n");return VKINIT_OK;
}
void CloseGraphicsDevice(void){
 bgClear();
 for(int i=1;i<=tn;++i)if(tex[i].u){if(tex[i].samp)wgpuSamplerRelease(tex[i].samp);if(tex[i].v)wgpuTextureViewRelease(tex[i].v);if(tex[i].t)wgpuTextureRelease(tex[i].t);}
 for(int i=1;i<=mn;++i){free(meshes[i].v);free(meshes[i].i);}
 memset(tex,0,sizeof tex);memset(meshes,0,sizeof meshes);memset(models,0,sizeof models);memset(mats,0,sizeof mats);
 tn=mn=mon=man=0;
 releaseIbl();
 if(g_iblFallbackCubeView)wgpuTextureViewRelease(g_iblFallbackCubeView);
 if(g_iblFallback2DView)wgpuTextureViewRelease(g_iblFallback2DView);
 if(g_iblFallbackCubeTex)wgpuTextureRelease(g_iblFallbackCubeTex);
 if(g_iblFallback2DTex)wgpuTextureRelease(g_iblFallback2DTex);
 if(g_iblSamp)wgpuSamplerRelease(g_iblSamp);
 if(g_shadowView)wgpuTextureViewRelease(g_shadowView);
 if(g_shadowTex)wgpuTextureRelease(g_shadowTex);
 if(g_spotView)wgpuTextureViewRelease(g_spotView);
 if(g_spot2View)wgpuTextureViewRelease(g_spot2View);if(g_spot2Tex)wgpuTextureRelease(g_spot2Tex);
 if(g_spot3View)wgpuTextureViewRelease(g_spot3View);if(g_spot3Tex)wgpuTextureRelease(g_spot3Tex);
 if(g_spot4View)wgpuTextureViewRelease(g_spot4View);if(g_spot4Tex)wgpuTextureRelease(g_spot4Tex);
 if(g_spotTex)wgpuTextureRelease(g_spotTex);
 if(g_pointView)wgpuTextureViewRelease(g_pointView);
 if(g_point2View)wgpuTextureViewRelease(g_point2View);
 for(int f=0;f<6;++f)if(g_point2FaceView[f])wgpuTextureViewRelease(g_point2FaceView[f]);
 if(g_point2Tex)wgpuTextureRelease(g_point2Tex);
 if(g_point3View)wgpuTextureViewRelease(g_point3View);
 for(int f=0;f<6;++f)if(g_point3FaceView[f])wgpuTextureViewRelease(g_point3FaceView[f]);
 if(g_point3Tex)wgpuTextureRelease(g_point3Tex);
 if(g_pointArrView)wgpuTextureViewRelease(g_pointArrView);
 for(int p=0;p<2;++p)for(int f=0;f<6;++f)if(g_pointArrFace[p][f])wgpuTextureViewRelease(g_pointArrFace[p][f]);
 if(g_pointArrTex)wgpuTextureRelease(g_pointArrTex);
 for(int f=0;f<6;++f)if(g_pointFaceView[f])wgpuTextureViewRelease(g_pointFaceView[f]);
 if(g_pointTex)wgpuTextureRelease(g_pointTex);
 if(g_shadowSamp)wgpuSamplerRelease(g_shadowSamp);if(g_pointRawSamp)wgpuSamplerRelease(g_pointRawSamp);
 if(g_shadowUBO)wgpuBufferRelease(g_shadowUBO);
 if(g_shadowVB)wgpuBufferRelease(g_shadowVB);
 if(g_shadowUBO2)wgpuBufferRelease(g_shadowUBO2);
 if(g_shadowVB2)wgpuBufferRelease(g_shadowVB2);
 if(g_shadowUBO3)wgpuBufferRelease(g_shadowUBO3);
 if(g_shadowVB3)wgpuBufferRelease(g_shadowVB3);
 if(g_shadowUBO4)wgpuBufferRelease(g_shadowUBO4);
 if(g_shadowVB4)wgpuBufferRelease(g_shadowVB4);
 if(g_spotUBO)wgpuBufferRelease(g_spotUBO);
 if(g_spotVB)wgpuBufferRelease(g_spotVB);
 if(g_spot2UBO)wgpuBufferRelease(g_spot2UBO);
 if(g_spot2VB)wgpuBufferRelease(g_spot2VB);
 if(g_spot3UBO)wgpuBufferRelease(g_spot3UBO);
 if(g_spot3VB)wgpuBufferRelease(g_spot3VB);
 if(g_spot4UBO)wgpuBufferRelease(g_spot4UBO);
 if(g_spot4VB)wgpuBufferRelease(g_spot4VB);
 for(int f=0;f<6;++f){if(g_pointUBO[f])wgpuBufferRelease(g_pointUBO[f]);
  if(g_pointVB[f])wgpuBufferRelease(g_pointVB[f]);
  if(g_point2UBO[f])wgpuBufferRelease(g_point2UBO[f]);
  if(g_point2VB[f])wgpuBufferRelease(g_point2VB[f]);
  if(g_point3UBO[f])wgpuBufferRelease(g_point3UBO[f]);
  if(g_point3VB[f])wgpuBufferRelease(g_point3VB[f]);}
 if(g_pShadow)wgpuRenderPipelineRelease(g_pShadow);
 if(g_bglShadow)wgpuBindGroupLayoutRelease(g_bglShadow);
 g_shadowView=0;g_shadowTex=0;g_spotView=0;g_spotTex=0;g_spot2View=0;g_spot2Tex=0;g_spot3View=0;g_spot3Tex=0;g_spot4View=0;g_spot4Tex=0;g_pointView=0;g_pointTex=0;g_shadowSamp=0;g_shadowUBO=0;g_shadowVB=0;g_shadowUBO2=0;g_shadowVB2=0;
 g_shadowUBO3=0;g_shadowVB3=0;g_shadowUBO4=0;g_shadowVB4=0;g_spotUBO=0;g_spotVB=0;g_spot2UBO=0;g_spot2VB=0;g_spot3UBO=0;g_spot3VB=0;g_spot4UBO=0;g_spot4VB=0;
 for(int f=0;f<6;++f){g_pointFaceView[f]=0;g_pointUBO[f]=0;g_pointVB[f]=0;
  g_point2FaceView[f]=0;g_point2UBO[f]=0;g_point2VB[f]=0;
  g_point3FaceView[f]=0;g_point3UBO[f]=0;g_point3VB[f]=0;
  g_pointArrFace[0][f]=0;g_pointArrFace[1][f]=0;g_pointArrFace[2][f]=0;g_pointArrFace[3][f]=0;}g_point2View=0;g_point2Tex=0;g_point3View=0;g_point3Tex=0;g_pointArrView=0;g_pointArrTex=0;g_pShadow=0;g_bglShadow=0;
 if(g_wview)wgpuTextureViewRelease(g_wview);if(g_wtex)wgpuTextureRelease(g_wtex);
 if(g_nview)wgpuTextureViewRelease(g_nview);if(g_ntex)wgpuTextureRelease(g_ntex);
 if(g_eview)wgpuTextureViewRelease(g_eview);if(g_etex)wgpuTextureRelease(g_etex);if(g_samp)wgpuSamplerRelease(g_samp);
 if(g_ubo)wgpuBufferRelease(g_ubo);if(g_vb3)wgpuBufferRelease(g_vb3);if(g_vb2)wgpuBufferRelease(g_vb2);
 if(g_bgl2)wgpuBindGroupLayoutRelease(g_bgl2);if(g_bglGlob)wgpuBindGroupLayoutRelease(g_bglGlob);if(g_bglMat)wgpuBindGroupLayoutRelease(g_bglMat);if(g_bgl)wgpuBindGroupLayoutRelease(g_bgl);if(g_plines)wgpuRenderPipelineRelease(g_plines);
 if(g_pbr)wgpuRenderPipelineRelease(g_pbr);if(g_p2)wgpuRenderPipelineRelease(g_p2);
 if(g_bgSsao)wgpuBindGroupRelease(g_bgSsao);g_bgSsao=0;
 if(g_depthV)wgpuTextureViewRelease(g_depthV);if(g_depth)wgpuTextureRelease(g_depth);
 if(g_bgc)wgpuBindGroupRelease(g_bgc);g_bgc=0;
 if(g_bgBloom)wgpuBindGroupRelease(g_bgBloom);g_bgBloom=0;
 if(g_bgSsaoComp)wgpuBindGroupRelease(g_bgSsaoComp);g_bgSsaoComp=0;
 postGraphFree();
 if(g_pcomp)wgpuRenderPipelineRelease(g_pcomp);if(g_pcompFxaa)wgpuRenderPipelineRelease(g_pcompFxaa);
 if(g_pblit)wgpuRenderPipelineRelease(g_pblit);
 if(g_pbright)wgpuRenderPipelineRelease(g_pbright);if(g_pcompBloom)wgpuRenderPipelineRelease(g_pcompBloom);
 if(g_pssao)wgpuRenderPipelineRelease(g_pssao);if(g_pcompSsao)wgpuRenderPipelineRelease(g_pcompSsao);
 if(g_pTaa)wgpuRenderPipelineRelease(g_pTaa);if(g_pVel)wgpuRenderPipelineRelease(g_pVel);
 g_pcomp=0;g_pcompFxaa=0;g_pblit=0;g_pbright=0;g_pcompBloom=0;g_pssao=0;g_pcompSsao=0;g_pTaa=0;g_pVel=0;
 if(g_bglp)wgpuBindGroupLayoutRelease(g_bglp);if(g_bgls)wgpuBindGroupLayoutRelease(g_bgls);if(g_bgls2)wgpuBindGroupLayoutRelease(g_bgls2);
 if(g_bglTaa)wgpuBindGroupLayoutRelease(g_bglTaa);if(g_bglVel)wgpuBindGroupLayoutRelease(g_bglVel);
 g_bglp=0;g_bgls=0;g_bgls2=0;g_bglTaa=0;g_bglVel=0;
 if(g_ssaoUBO)wgpuBufferRelease(g_ssaoUBO);g_ssaoUBO=0;
 if(g_taaUBO)wgpuBufferRelease(g_taaUBO);g_taaUBO=0;
 if(g_velUBO)wgpuBufferRelease(g_velUBO);g_velUBO=0;
 if(g_velVB)wgpuBufferRelease(g_velVB);g_velVB=0;
 if(g_bglc)wgpuBindGroupLayoutRelease(g_bglc);g_bglc=0;
 if(g_sceneView)wgpuTextureViewRelease(g_sceneView);g_sceneView=0;
 if(g_bgFinal)wgpuBindGroupRelease(g_bgFinal);g_bgFinal=0;
 if(g_finalView)wgpuTextureViewRelease(g_finalView);g_finalView=0;
 if(g_finalTex)wgpuTextureRelease(g_finalTex);g_finalTex=0;
 if(g_sceneTex)wgpuTextureRelease(g_sceneTex);g_sceneTex=0;
 if(g_bloomView)wgpuTextureViewRelease(g_bloomView);g_bloomView=0;
 if(g_bloomTex)wgpuTextureRelease(g_bloomTex);g_bloomTex=0;
 if(g_ssaoView)wgpuTextureViewRelease(g_ssaoView);g_ssaoView=0;
 if(g_ssaoTex)wgpuTextureRelease(g_ssaoTex);g_ssaoTex=0;
 for(int i=0;i<2;++i){if(g_taaView[i])wgpuTextureViewRelease(g_taaView[i]);g_taaView[i]=0;
  if(g_taaTex[i])wgpuTextureRelease(g_taaTex[i]);g_taaTex[i]=0;}
 if(g_velView)wgpuTextureViewRelease(g_velView);g_velView=0;
 if(g_velTex)wgpuTextureRelease(g_velTex);g_velTex=0;
 if(g_pskin)wgpuRenderPipelineRelease(g_pskin);
 if(g_pshadowSkin)wgpuRenderPipelineRelease(g_pshadowSkin);
 if(g_pVelSkin)wgpuRenderPipelineRelease(g_pVelSkin);
 if(g_bgSkin)wgpuBindGroupRelease(g_bgSkin);
 if(g_bgSkinVel)wgpuBindGroupRelease(g_bgSkinVel);
 if(g_bglSkin)wgpuBindGroupLayoutRelease(g_bglSkin);
 if(g_bglSkinVel)wgpuBindGroupLayoutRelease(g_bglSkinVel);
 g_pskin=0;g_pshadowSkin=0;g_pVelSkin=0;g_bgSkin=0;g_bgSkinVel=0;g_bglSkin=0;g_bglSkinVel=0;g_bglMat=0;g_bglGlob=0;
 if(g_skiVB)wgpuBufferRelease(g_skiVB);g_skiVB=0;
 if(g_skiUBO)wgpuBufferRelease(g_skiUBO);g_skiUBO=0;
 if(g_velSkVB)wgpuBufferRelease(g_velSkVB);g_velSkVB=0;
 if(g_velSkUBO)wgpuBufferRelease(g_velSkUBO);g_velSkUBO=0;
 if(g_skinMod)wgpuShaderModuleRelease(g_skinMod);g_skinMod=0;
 free(skv);skv=0;skvCap=0;
 if(skModels){for(int i=1;i<VLCR_SKIN_MODELS;++i)if(skModels[i].u)skFreeModelEntry(&skModels[i]);free(skModels);skModels=0;}
 if(skAnims){for(int i=1;i<=skAnimN;++i)skFreeAnim(&skAnims[i]);free(skAnims);skAnims=0;}
 skAnimN=0;skAnimCap=0;nskc=0;
 if(g_tsSet)wgpuQuerySetRelease(g_tsSet);g_tsSet=0;
 for(int i=0;i<2;++i){if(g_tsBuf[i])wgpuBufferRelease(g_tsBuf[i]);g_tsBuf[i]=0;
  if(g_tsResolve[i])wgpuBufferRelease(g_tsResolve[i]);g_tsResolve[i]=0;}
 if(g_swapchain)wgpuSwapChainRelease(g_swapchain);g_swapchain=0;
 if(g_surf)wgpuSurfaceRelease(g_surf);if(g_q)wgpuQueueRelease(g_q);if(g_dev)wgpuDeviceRelease(g_dev);if(g_inst)wgpuInstanceRelease(g_inst);g_ok=0;
}
void SetVulkanClearColor(float r,float g,float b,float a){g_clear[0]=r;g_clear[1]=g;g_clear[2]=b;g_clear[3]=a;}
void SetVulkanViewport(int x,int y,int w,int h){(void)x;(void)y;if(w>0&&h>0){gw=w;gh=h;cfg();}}
void SetVulkanViewMatrix(Matrix m){g_view=m;} void SetVulkanProjectionMatrix(Matrix m){g_proj=m;}
void vgraphics_Resize(void){cfg();} void vgraphics_Set2DViewportSize(int w,int h){SetVulkanViewport(0,0,w,h);}
void vgraphics_GetRenderTargetSize(int *w,int *h){if(w)*w=gw;if(h)*h=gh;}
static void releaseIbl(void){
 if(g_iblIrrView)wgpuTextureViewRelease(g_iblIrrView);if(g_iblPreView)wgpuTextureViewRelease(g_iblPreView);
 if(g_iblBrdfView)wgpuTextureViewRelease(g_iblBrdfView);if(g_iblIrrTex)wgpuTextureRelease(g_iblIrrTex);
 if(g_iblPreTex)wgpuTextureRelease(g_iblPreTex);if(g_iblBrdfTex)wgpuTextureRelease(g_iblBrdfTex);
 g_iblIrrView=g_iblPreView=g_iblBrdfView=0;g_iblIrrTex=g_iblPreTex=g_iblBrdfTex=0;g_iblLoaded=0;
}
static uint32_t rowStride(int width){uint32_t row=(uint32_t)width*(g_f32Filterable?16u:8u);return (row+255u)&~255u;}
static bool uploadCube(const VIBL_Cube *cube,WGPUTexture texture){
 if(!cube||!cube->faces||cube->size<1||!texture)return false;
 int mips=cube->mips>0?cube->mips:1;
 for(int m=0;m<mips;++m){
  int s=cube->size>>m;if(s<1)s=1;uint32_t stride=rowStride(s);size_t bytes=(size_t)stride*(size_t)s;
  unsigned char *rgba=(unsigned char*)calloc(bytes,1);if(!rgba)return false;
  for(int f=0;f<6;++f){
   const float *src=cube->faces[m*6+f];
   for(int y=0;y<s;++y){
    if(g_f32Filterable){
     float *row=(float*)(rgba+(size_t)y*stride);
     for(int x=0;x<s;++x){size_t i=(size_t)y*(size_t)s+(size_t)x;
      row[x*4+0]=src?src[i*3+0]:0.0f;row[x*4+1]=src?src[i*3+1]:0.0f;
      row[x*4+2]=src?src[i*3+2]:0.0f;row[x*4+3]=1.0f;}
    }else{
     uint16_t *row=(uint16_t*)(rgba+(size_t)y*stride);
     for(int x=0;x<s;++x){size_t i=(size_t)y*(size_t)s+(size_t)x;
      row[x*4+0]=f2h(src?src[i*3+0]:0.0f);row[x*4+1]=f2h(src?src[i*3+1]:0.0f);
      row[x*4+2]=f2h(src?src[i*3+2]:0.0f);row[x*4+3]=f2h(1.0f);}
    }
   }
   WGPUImageCopyTexture dst={0};dst.texture=texture;dst.mipLevel=(uint32_t)m;dst.origin.z=(uint32_t)f;dst.aspect=WGPUTextureAspect_All;
   WGPUTextureDataLayout lay={0};lay.bytesPerRow=stride;lay.rowsPerImage=(uint32_t)s;
   WGPUExtent3D ext={(uint32_t)s,(uint32_t)s,1};
   wgpuQueueWriteTexture(g_q,&dst,rgba,bytes,&lay,&ext);
  }
  free(rgba);
 }
 return true;
}
static bool uploadBrdf(const VIBL_LUT *lut,WGPUTexture texture){
 if(!lut||!lut->data||lut->size<1||!texture)return false;
 int s=lut->size;uint32_t stride=rowStride(s);size_t bytes=(size_t)stride*(size_t)s;
 unsigned char *rgba=(unsigned char*)calloc(bytes,1);if(!rgba)return false;
 for(int y=0;y<s;++y){
  if(g_f32Filterable){
   float *row=(float*)(rgba+(size_t)y*stride);
   for(int x=0;x<s;++x){size_t i=(size_t)y*(size_t)s+(size_t)x;
    row[x*4+0]=lut->data[i*2+0];row[x*4+1]=lut->data[i*2+1];row[x*4+2]=0;row[x*4+3]=1.0f;}
  }else{
   uint16_t *row=(uint16_t*)(rgba+(size_t)y*stride);
   for(int x=0;x<s;++x){size_t i=(size_t)y*(size_t)s+(size_t)x;
    row[x*4+0]=f2h(lut->data[i*2+0]);row[x*4+1]=f2h(lut->data[i*2+1]);row[x*4+2]=0;row[x*4+3]=f2h(1.0f);}
  }
 }
 WGPUImageCopyTexture dst={0};dst.texture=texture;dst.aspect=WGPUTextureAspect_All;
 WGPUTextureDataLayout lay={0};lay.bytesPerRow=stride;lay.rowsPerImage=(uint32_t)s;
 WGPUExtent3D ext={(uint32_t)s,(uint32_t)s,1};
 wgpuQueueWriteTexture(g_q,&dst,rgba,bytes,&lay,&ext);
 free(rgba);return true;
}
static bool uploadIblBundle(const VIBL_Bundle *b){
 if(!b||!b->valid||!g_dev)return false;
 releaseIbl();
 int preMips=b->prefiltered.mips>0?b->prefiltered.mips:1;
 g_iblIrrTex=iblCubeTexture(b->irradiance.size>0?b->irradiance.size:16,1);
 g_iblPreTex=iblCubeTexture(b->prefiltered.size>0?b->prefiltered.size:64,preMips);
 g_iblBrdfTex=ibl2DTexture(b->brdf.size>0?b->brdf.size:64,b->brdf.size>0?b->brdf.size:64);
 if(!g_iblIrrTex||!g_iblPreTex||!g_iblBrdfTex){releaseIbl();return false;}
 g_iblIrrView=iblCubeView(g_iblIrrTex,1);
 g_iblPreView=iblCubeView(g_iblPreTex,preMips);
 g_iblBrdfView=ibl2DView(g_iblBrdfTex);
 if(!g_iblIrrView||!g_iblPreView||!g_iblBrdfView){releaseIbl();return false;}
 if(!uploadCube(&b->irradiance,g_iblIrrTex)||!uploadCube(&b->prefiltered,g_iblPreTex)||
    !uploadBrdf(&b->brdf,g_iblBrdfTex)){releaseIbl();return false;}
 g_iblMips=preMips;g_iblLoaded=1;return true;
}
void vgraphics_DrawSkybox(void){}
void vgraphics_FlushTransparent(void){}
bool vgraphics_LoadIBL(const char *path){
 VIBL_Bundle b=(path&&path[0])?VIBL_BuildFromHDRFile(path,64,16):VIBL_BuildProcedural(64,16);
 if(!b.valid){fprintf(stderr,"[webgpu] IBL bake failed\n");return false;}
 bool ok=uploadIblBundle(&b);VIBL_FreeBundle(&b);if(ok)bgClear();
 if(ok)g_iblIntensity=1.0f;
 return ok;
}
void vgraphics_SetIBLIntensity(float i){if(i<0)i=0;if(i>4)i=4;g_iblIntensity=i;}
void vgraphics_SetSkybox(int e){(void)e;}
void SetSSAO(int e,float r,float i){g_ssaoEnabled=e?1:0;if(r>0)g_ssaoRadius=r;g_ssaoIntensity=i;}
void SetBloom(float t,float i){(void)t;g_bloomEnabled=i>0.001f;}
void SetPostProcess(int e){(void)e;}
void SetGraphicsExposure(float e){g_exposure=e<0.01f?0.01f:(e>16.0f?16.0f:e);}
bool vgraphics_TrackModelTexture(Model m,Texture2D t){(void)m;(void)t;return true;}
Texture2D LoadTextureKTX2(const char *path){
 Texture2D o={0};if(!g_dev||(!g_bcSupported&&!g_etc2Supported&&!g_astcSupported)||!path||!path[0]||tn+1>=MT)return o;
 FILE*f=fopen(path,"rb");if(!f)return o;unsigned char h[80];
 if(fread(h,1,80,f)!=80||memcmp(h,"\xABKTX 20\xBB\r\n\x1A\n",12)!=0){fclose(f);return o;}
 uint32_t vkFormat,w,hh,levels,supercomp;
 memcpy(&vkFormat,h+12,4);memcpy(&w,h+20,4);memcpy(&hh,h+24,4);memcpy(&levels,h+40,4);memcpy(&supercomp,h+44,4);
 if(supercomp!=0||levels==0||w==0||hh==0||levels>16){fclose(f);return o;}
 WGPUTextureFormat fmt;int srgb=0,isBc5=0,blockBytes=16;
 if(vkFormat==145){if(!g_bcSupported){fclose(f);return o;}fmt=WGPUTextureFormat_BC7RGBAUnorm;}
 else if(vkFormat==146){if(!g_bcSupported){fclose(f);return o;}fmt=WGPUTextureFormat_BC7RGBAUnormSrgb;srgb=1;}
 else if(vkFormat==143){if(!g_bcSupported){fclose(f);return o;}fmt=WGPUTextureFormat_BC5RGUnorm;isBc5=1;}
 else if(vkFormat==144){if(!g_bcSupported){fclose(f);return o;}fmt=WGPUTextureFormat_BC5RGSnorm;isBc5=1;}
 else if(vkFormat==147){if(!g_etc2Supported){fclose(f);return o;}fmt=WGPUTextureFormat_ETC2RGB8Unorm;blockBytes=8;}
 else if(vkFormat==148){if(!g_etc2Supported){fclose(f);return o;}fmt=WGPUTextureFormat_ETC2RGB8UnormSrgb;srgb=1;blockBytes=8;}
 else if(vkFormat==157){if(!g_astcSupported){fclose(f);return o;}fmt=WGPUTextureFormat_ASTC4x4Unorm;blockBytes=16;}
 else if(vkFormat==158){if(!g_astcSupported){fclose(f);return o;}fmt=WGPUTextureFormat_ASTC4x4UnormSrgb;srgb=1;blockBytes=16;}
 else{fclose(f);return o;}
 uint64_t off[16]={0},len[16]={0};
 if(levels>16)levels=16;
 if(fseek(f,80,SEEK_SET)!=0){fclose(f);return o;}
 for(uint32_t l=0;l<levels;++l){uint64_t u=0;
  if(fread(&off[l],8,1,f)!=1||fread(&len[l],8,1,f)!=1||fread(&u,8,1,f)!=1){fclose(f);return o;}
  if(len[l]==0||len[l]>64ull*1024*1024||off[l]>64ull*1024*1024){fclose(f);return o;}}
 int id=++tn;WT*t=&tex[id];memset(t,0,sizeof*t);t->u=1;t->w=(int)w;t->h=(int)hh;t->mips=(int)levels;t->srgb=srgb;t->bc5=isBc5;
 WGPUTextureDescriptor td={0};td.size.width=w;td.size.height=hh;td.size.depthOrArrayLayers=1;
 td.mipLevelCount=levels;td.sampleCount=1;td.dimension=WGPUTextureDimension_2D;td.format=fmt;
 td.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;
 t->t=wgpuDeviceCreateTexture(g_dev,&td);t->v=wgpuTextureCreateView(t->t,NULL);
 for(uint32_t l=0;l<levels;++l){
  int lw=(int)(w>>l),lh=(int)(hh>>l);if(lw<1)lw=1;if(lh<1)lh=1;
  int bx=(lw+3)/4,by=(lh+3)/4;uint32_t row=(uint32_t)bx*(uint32_t)blockBytes,stride=(row+255u)&~255u;
  size_t bytes=(size_t)stride*(size_t)by;unsigned char*tmp=(unsigned char*)calloc(bytes,1);
  unsigned char*src=(unsigned char*)malloc((size_t)len[l]);if(!tmp||!src){free(tmp);free(src);fclose(f);UnloadTexture((Texture2D){(unsigned)id,0,0});return (Texture2D){0};}
  if(fseek(f,(long)off[l],SEEK_SET)!=0||fread(src,1,(size_t)len[l],f)!=(size_t)len[l]){free(tmp);free(src);fclose(f);UnloadTexture((Texture2D){(unsigned)id,0,0});return (Texture2D){0};}
  for(int y=0;y<by;++y)memcpy(tmp+(size_t)y*stride,src+(size_t)y*row,(size_t)row);
  WGPUImageCopyTexture d={0};d.texture=t->t;d.mipLevel=l;d.aspect=WGPUTextureAspect_All;
  WGPUTextureDataLayout dl={0};dl.bytesPerRow=stride;dl.rowsPerImage=(uint32_t)by;
  WGPUExtent3D e={(uint32_t)lw,(uint32_t)lh,1};wgpuQueueWriteTexture(g_q,&d,tmp,bytes,&dl,&e);
  free(tmp);free(src);
 }
 fclose(f);o.id=(unsigned)id;o.width=(int)w;o.height=(int)hh;return o;
}
void vgraphics_Set3DMatrices(Matrix v,Matrix p){
 /* MatrixPerspective negates Y for Vulkan's y-down NDC; WebGPU NDC is y-up,
    so mirror clip Y back or the whole 3D scene renders vertically flipped. */
 p.m1=-p.m1;p.m5=-p.m5;p.m9=-p.m9;p.m13=-p.m13;
 g_view=v;g_proj=p;
} void vgraphics_SetCameraPos(Vector3 p){g_cam=p;}
void vgraphics_SetDirectionalLight(bool on,Vector3 d,Color c,float i){g_lon=on?1:0;float L=sqrtf(d.x*d.x+d.y*d.y+d.z*d.z);if(L>1e-6f){d.x/=L;d.y/=L;d.z/=L;}g_ld=d;g_lc[0]=c.r/255.f;g_lc[1]=c.g/255.f;g_lc[2]=c.b/255.f;g_li=i;}
void vgraphics_SetAmbientLight(bool on,Color c,float i){g_ac[0]=c.r/255.f;g_ac[1]=c.g/255.f;g_ac[2]=c.b/255.f;g_ai=on?i:0.0f;}
void BeginVulkanFrame(void){n2=n3=nl=nc=nb2c=nskc=0;b2cur_tex=0;
 g_statDraws=0;g_statShadowDraws=0;g_statTris=0;g_frameStart=emscripten_get_now();
 if(g_tsSupported){int p=g_tsPrev;
  /* Only touch the readback buffer once its map actually resolved. Calling
     Unmap while the map is still pending (typical when several frames are
     recorded inside one event-loop tick) leaves the map queued, and the next
     submit that copies into this buffer is rejected with "used in submit
     while mapped". */
  if(p>=0&&p<2&&g_tsBuf[p]&&wgpuBufferGetMapState(g_tsBuf[p])==WGPUBufferMapState_Mapped){
   const uint64_t*d=(const uint64_t*)wgpuBufferGetConstMappedRange(g_tsBuf[p],0,16);
   if(g_tsMapped[p]&&d&&d[1]>d[0]){g_gpuFrameMs=(float)((double)(d[1]-d[0])*1e-6);g_tsSampled=1;}
   wgpuBufferUnmap(g_tsBuf[p]);g_tsMapped[p]=0;}}
}

static void fillu(UBO*u,Matrix model,Color bc,float met,float rou){
 Matrix mvp=MatrixMultiply(g_proj,MatrixMultiply(g_view,model));mcm(&mvp,u->mvp);mcm(&model,u->model);
 u->ld[0]=g_ld.x;u->ld[1]=g_ld.y;u->ld[2]=g_ld.z;u->ld[3]=0;u->lc[0]=g_lc[0];u->lc[1]=g_lc[1];u->lc[2]=g_lc[2];u->lc[3]=g_lon?g_li:0;
 u->amb[0]=g_ac[0];u->amb[1]=g_ac[1];u->amb[2]=g_ac[2];u->amb[3]=g_ai;u->cam[0]=g_cam.x;u->cam[1]=g_cam.y;u->cam[2]=g_cam.z;u->cam[3]=1;
 u->bc[0]=bc.r/255.f;u->bc[1]=bc.g/255.f;u->bc[2]=bc.b/255.f;u->bc[3]=bc.a/255.f;u->mr[0]=met;u->mr[1]=rou;u->mr[2]=0;u->mr[3]=0;
 u->ibl[0]=g_iblLoaded?g_iblIntensity:0.0f;u->ibl[1]=(float)(g_iblMips>0?g_iblMips:1);u->ibl[2]=0;u->ibl[3]=0;
 mcm(&g_shadowVP,u->shadowVP);u->shadowParams[0]=g_shadowBias;u->shadowParams[1]=g_shadowStrength;
 u->shadowParams[2]=g_shadowTexel;u->shadowParams[3]=(g_shadowEnabled&&g_shadowValid)?1.0f:0.0f;
 mcm(&g_shadowVP2,u->shadowVP2);mcm(&g_shadowVP3,u->shadowVP3);mcm(&g_shadowVP4,u->shadowVP4);
 u->shadowCfg[0]=g_shadowSoftness;u->shadowCfg[1]=0.0f;u->shadowCfg[2]=1.0f/2048.0f;u->shadowCfg[3]=1.0f/2048.0f;
 for(int i=0;i<4;++i)u->shadowSplits[i]=g_shadowSplits[i];
 mcm(&g_spotVP,u->spotVP);
 u->spotLight[0]=g_spotPos.x;u->spotLight[1]=g_spotPos.y;u->spotLight[2]=g_spotPos.z;u->spotLight[3]=g_spotRange;
 u->spotDir[0]=g_spotDir.x;u->spotDir[1]=g_spotDir.y;u->spotDir[2]=g_spotDir.z;u->spotDir[3]=g_spotCos;
 u->spotColor[0]=g_spotColor.r/255.f;u->spotColor[1]=g_spotColor.g/255.f;u->spotColor[2]=g_spotColor.b/255.f;u->spotColor[3]=g_spotIntensity;
 u->spotParams[0]=g_spotOn?1.0f:0.0f;u->spotParams[1]=0.002f;u->spotParams[2]=1.0f/1024.0f;u->spotParams[3]=0.8f;
 for(int i=0;i<4;++i){PointL*l=&g_points[i];u->pointPos[i*4+0]=l->pos.x;u->pointPos[i*4+1]=l->pos.y;u->pointPos[i*4+2]=l->pos.z;
  u->pointPos[i*4+3]=l->on?l->range:0.0f;u->pointColor[i*4+0]=l->color.r/255.f;u->pointColor[i*4+1]=l->color.g/255.f;
  u->pointColor[i*4+2]=l->color.b/255.f;u->pointColor[i*4+3]=l->intensity;}
 for(int f=0;f<6;++f)mcm(&g_pointVP[f],u->pointVP+f*16);
 u->pointShadowParams[0]=g_points[0].on?1.0f:0.0f;u->pointShadowParams[1]=0.003f;
 u->pointShadowParams[2]=0.0f;u->pointShadowParams[3]=0.8f;
 for(int f=0;f<6;++f)mcm(&g_point2VP[f],u->point2VP+f*16);
 u->point2Params[0]=g_points[1].on?1.0f:0.0f;u->point2Params[1]=0.003f;u->point2Params[2]=0.0f;u->point2Params[3]=0.8f;
 for(int f=0;f<6;++f)mcm(&g_point3VP[f],u->point3VP+f*16);
 u->point3Params[0]=g_points[2].on?1.0f:0.0f;u->point3Params[1]=0.003f;u->point3Params[2]=0.0f;u->point3Params[3]=0.8f;
 for(int f=0;f<6;++f)mcm(&g_point4VP[f],u->point4VP+f*16);
 u->point4Params[0]=g_points[3].on?1.0f:0.0f;u->point4Params[1]=0.003f;u->point4Params[2]=0.0f;u->point4Params[3]=0.8f;
 u->exposure[0]=g_exposure;u->exposure[1]=0;u->exposure[2]=0;u->exposure[3]=0;
 for(int i=0;i<3;++i){ExtraSpot*s=&g_extraSpots[i];
  u->extraSpotPos[i*4+0]=s->pos.x;u->extraSpotPos[i*4+1]=s->pos.y;u->extraSpotPos[i*4+2]=s->pos.z;
  u->extraSpotPos[i*4+3]=s->on?s->range:0.0f;
  u->extraSpotDir[i*4+0]=s->dir.x;u->extraSpotDir[i*4+1]=s->dir.y;u->extraSpotDir[i*4+2]=s->dir.z;u->extraSpotDir[i*4+3]=s->cos;
  u->extraSpotColor[i*4+0]=s->cr;u->extraSpotColor[i*4+1]=s->cg;u->extraSpotColor[i*4+2]=s->cb;u->extraSpotColor[i*4+3]=1.0f;
  u->extraSpotParams[i*4+0]=s->on?1.0f:0.0f;u->extraSpotParams[i*4+1]=0.0f;u->extraSpotParams[i*4+2]=0.0f;u->extraSpotParams[i*4+3]=0.0f;}
 mcm(&g_spot2VP,u->spot2VP);ExtraSpot*s0=&g_extraSpots[0];
 u->spot2Light[0]=s0->pos.x;u->spot2Light[1]=s0->pos.y;u->spot2Light[2]=s0->pos.z;u->spot2Light[3]=s0->range;
 u->spot2Dir[0]=s0->dir.x;u->spot2Dir[1]=s0->dir.y;u->spot2Dir[2]=s0->dir.z;u->spot2Dir[3]=s0->cos;
 u->spot2Color[0]=s0->cr;u->spot2Color[1]=s0->cg;u->spot2Color[2]=s0->cb;u->spot2Color[3]=1;
 u->spot2Params[0]=s0->on?1.0f:0.0f;u->spot2Params[1]=0.002f;u->spot2Params[2]=1.0f/1024.0f;u->spot2Params[3]=0.8f;
 mcm(&g_spot3VP,u->spot3VP);ExtraSpot*s1=&g_extraSpots[1];
 u->spot3Light[0]=s1->pos.x;u->spot3Light[1]=s1->pos.y;u->spot3Light[2]=s1->pos.z;u->spot3Light[3]=s1->range;
 u->spot3Dir[0]=s1->dir.x;u->spot3Dir[1]=s1->dir.y;u->spot3Dir[2]=s1->dir.z;u->spot3Dir[3]=s1->cos;
 u->spot3Color[0]=s1->cr;u->spot3Color[1]=s1->cg;u->spot3Color[2]=s1->cb;u->spot3Color[3]=1;
 u->spot3Params[0]=s1->on?1.0f:0.0f;u->spot3Params[1]=0.002f;u->spot3Params[2]=1.0f/1024.0f;u->spot3Params[3]=0.8f;
 mcm(&g_spot4VP,u->spot4VP);ExtraSpot*s2=&g_extraSpots[2];
 u->spot4Light[0]=s2->pos.x;u->spot4Light[1]=s2->pos.y;u->spot4Light[2]=s2->pos.z;u->spot4Light[3]=s2->range;
 u->spot4Dir[0]=s2->dir.x;u->spot4Dir[1]=s2->dir.y;u->spot4Dir[2]=s2->dir.z;u->spot4Dir[3]=s2->cos;
 u->spot4Color[0]=s2->cr;u->spot4Color[1]=s2->cg;u->spot4Color[2]=s2->cb;u->spot4Color[3]=1;
 u->spot4Params[0]=s2->on?1.0f:0.0f;u->spot4Params[1]=0.002f;u->spot4Params[2]=1.0f/1024.0f;u->spot4Params[3]=0.8f;
}
static WGPUBindGroup mkbg(unsigned a,unsigned m,unsigned n,unsigned o,unsigned em){
 for(int i=0;i<nbg;++i)if(bgCache[i].a==a&&bgCache[i].m==m&&bgCache[i].n==n&&bgCache[i].o==o&&bgCache[i].e==em)return bgCache[i].bg;
 if(nbg>=128)bgClear();
 WGPUBindGroupEntry e={0};e.binding=0;e.buffer=g_ubo;e.size=sizeof(UBO);
 WGPUBindGroupDescriptor d={0};d.layout=g_bgl;d.entryCount=1;d.entries=&e;WGPUBindGroup bg=wgpuDeviceCreateBindGroup(g_dev,&d);
 bgCache[nbg].a=a;bgCache[nbg].m=m;bgCache[nbg].n=n;bgCache[nbg].o=o;bgCache[nbg].e=em;bgCache[nbg].bg=bg;++nbg;return bg;
}
static WGPUBindGroup mkglob(void){
 WGPUBindGroupEntry e[13]={{0}};
 e[0].binding=4;e[0].textureView=g_iblLoaded?g_iblIrrView:g_iblFallbackCubeView;
 e[1].binding=5;e[1].textureView=g_iblLoaded?g_iblPreView:g_iblFallbackCubeView;
 e[2].binding=6;e[2].textureView=g_iblLoaded?g_iblBrdfView:g_iblFallback2DView;
 e[3].binding=7;e[3].sampler=g_iblSamp?g_iblSamp:g_samp;
 e[4].binding=8;e[4].textureView=g_shadowView;
 e[5].binding=9;e[5].sampler=g_shadowSamp;
 e[6].binding=10;e[6].textureView=g_spotView;
 e[7].binding=11;e[7].textureView=g_pointArrView;
 e[8].binding=15;e[8].textureView=g_pointArrView;
 e[9].binding=16;e[9].sampler=g_pointRawSamp;
 e[10].binding=17;e[10].textureView=g_spot2View;
 e[11].binding=18;e[11].textureView=g_spot3View;
 e[12].binding=19;e[12].textureView=g_spot4View;
 WGPUBindGroupDescriptor d={0};d.layout=g_bglGlob;d.entryCount=13;d.entries=e;
 return wgpuDeviceCreateBindGroup(g_dev,&d);
}
static WGPUBindGroup mkmat(unsigned a,unsigned m,unsigned n,unsigned o,unsigned em){
 WGPUBindGroupEntry e[6]={{0}};
 e[0].binding=0;e[0].sampler=(a<MT&&tex[a].samp)?tex[a].samp:g_samp;
 e[1].binding=1;e[1].textureView=tview(a);e[2].binding=2;e[2].textureView=tview(m);
 e[3].binding=3;e[3].textureView=n?tview(n):g_nview;e[4].binding=4;e[4].textureView=o?tview(o):g_wview;
 e[5].binding=5;e[5].textureView=em?tview(em):g_eview;
 WGPUBindGroupDescriptor d={0};d.layout=g_bglMat;d.entryCount=6;d.entries=e;
 return wgpuDeviceCreateBindGroup(g_dev,&d);
}
static void draw_cmd(WGPURenderPassEncoder pass,const DC*c,uint32_t*uboSend,uint32_t*vbSend){
 if(c->mid<=0||c->mid>=MM||!meshes[c->mid].u)return; WM*mesh=&meshes[c->mid];
 Color bc={255,255,255,255}; float met=0,rou=.5f; unsigned al=0,mr=0,nm=0,ao=0,em=0;
 if(c->aid>0&&c->aid<MA&&mats[c->aid].u){WA*ma=&mats[c->aid];bc=ma->bc;met=ma->met;rou=ma->rou;al=ma->a;mr=ma->m;nm=ma->n;ao=ma->o;em=ma->e;}
 bc.r=(unsigned char)((bc.r*c->tint.r)/255);bc.g=(unsigned char)((bc.g*c->tint.g)/255);bc.b=(unsigned char)((bc.b*c->tint.b)/255);bc.a=(unsigned char)((bc.a*c->tint.a)/255);
 int cnt=0; for(int i=0;i<mesh->ic&&cnt<65536;++i){uint32_t ix=mesh->i?mesh->i[i]:(uint32_t)i; if((int)ix>=mesh->vc)continue; b3[cnt++]=mesh->v[ix];}
 if(!cnt)return;
 uint64_t vbytes=(uint64_t)cnt*sizeof(V3);
 if(*uboSend+UBO_STRIDE>(uint32_t)MAX_DRAWS*UBO_STRIDE)return;
 if((uint64_t)*vbSend+vbytes>(uint64_t)MAX_FRAME_VERTS*sizeof(V3))return;
 UBO u; fillu(&u,c->model,bc,met,rou);u.mr[2]=(nm&&nm<MT&&tex[nm].bc5)?1.0f:0.0f;
 uint32_t uboOffset=*uboSend;
 wgpuQueueWriteBuffer(g_q,g_ubo,uboOffset,&u,sizeof u);
 WGPUBindGroup bg=mkbg(al,mr,nm,ao,em),glob=mkglob(),mat=mkmat(al,mr,nm,ao,em); wgpuQueueWriteBuffer(g_q,g_vb3,*vbSend,b3,(size_t)vbytes);
 wgpuRenderPassEncoderSetPipeline(pass,g_pbr); wgpuRenderPassEncoderSetBindGroup(pass,0,bg,1,&uboOffset);
 wgpuRenderPassEncoderSetBindGroup(pass,1,glob,0,NULL);wgpuRenderPassEncoderSetBindGroup(pass,2,mat,0,NULL);
 wgpuRenderPassEncoderSetVertexBuffer(pass,0,g_vb3,*vbSend,vbytes); wgpuRenderPassEncoderDraw(pass,(uint32_t)cnt,1,0,0);
 g_statDraws++;g_statTris+=(uint64_t)(cnt/3);
 *uboSend+=UBO_STRIDE; *vbSend+=(uint32_t)vbytes;
}
static void draw_shadow(WGPURenderPassEncoder pass,const DC*c,uint32_t*uboSend,uint32_t*vbSend,WGPUBuffer ubo,WGPUBuffer vb,const Matrix *vp){
 if(c->mid<=0||c->mid>=MM||!meshes[c->mid].u)return;WM*mesh=&meshes[c->mid];
 int cnt=0;for(int i=0;i<mesh->ic&&cnt<65536;++i){uint32_t ix=mesh->i?mesh->i[i]:(uint32_t)i;if((int)ix>=mesh->vc)continue;b3[cnt++]=mesh->v[ix];}
 if(!cnt)return;uint64_t vbytes=(uint64_t)cnt*sizeof(V3);
 if(*uboSend+UBO_STRIDE>(uint32_t)MAX_DRAWS*UBO_STRIDE)return;
 if((uint64_t)*vbSend+vbytes>(uint64_t)MAX_FRAME_VERTS*sizeof(V3))return;
 UBO u;memset(&u,0,sizeof u);mcm(&c->model,u.model);Matrix mvp=MatrixMultiply(*vp,c->model);mcm(&mvp,u.mvp);
 uint32_t off=*uboSend;wgpuQueueWriteBuffer(g_q,ubo,off,&u,sizeof u);
 WGPUBindGroup bg=mkbgShadow(ubo);wgpuQueueWriteBuffer(g_q,vb,*vbSend,b3,(size_t)vbytes);
 wgpuRenderPassEncoderSetPipeline(pass,g_pShadow);wgpuRenderPassEncoderSetBindGroup(pass,0,bg,1,&off);
 wgpuRenderPassEncoderSetVertexBuffer(pass,0,vb,*vbSend,vbytes);
 wgpuRenderPassEncoderDraw(pass,(uint32_t)cnt,1,0,0);wgpuBindGroupRelease(bg);g_statShadowDraws++;
 *uboSend+=UBO_STRIDE;*vbSend+=(uint32_t)vbytes;
}
/* ---- Skinned animation evaluation --------------------------------- */
static Matrix skLocalTmp[VLCR_SKIN_BONES];
static Matrix skGlobalTmp[VLCR_SKIN_BONES];
static unsigned char skDoneTmp[VLCR_SKIN_BONES];

static void skSampleVec(const SkVecKey*k,int n,float t,Vector3 def,Vector3*out){
 if(n<=0){*out=def;return;}
 if(t<=k[0].t){*out=k[0].v;return;}
 if(t>=k[n-1].t){*out=k[n-1].v;return;}
 for(int i=0;i<n-1;++i)if(t>=k[i].t&&t<=k[i+1].t){
  float span=k[i+1].t-k[i].t;float u=span>1e-6f?(t-k[i].t)/span:0.0f;
  *out=Vector3Lerp(k[i].v,k[i+1].v,u);return;
 }
 *out=k[n-1].v;
}
static void skSampleQuat(const SkQuatKey*k,int n,float t,Quaternion def,Quaternion*out){
 if(n<=0){*out=def;return;}
 if(t<=k[0].t){*out=k[0].q;return;}
 if(t>=k[n-1].t){*out=k[n-1].q;return;}
 for(int i=0;i<n-1;++i)if(t>=k[i].t&&t<=k[i+1].t){
  float span=k[i+1].t-k[i].t;float u=span>1e-6f?(t-k[i].t)/span:0.0f;
  *out=QuaternionSlerp(k[i].q,k[i+1].q,u);return;
 }
 *out=k[n-1].q;
}
static void skBoneGlobal(const SkSkel*s,int b,const Matrix*local,Matrix*g,unsigned char*done){
 if(done[b])return;
 int parent=s->bones[b].parent;
 if(parent>=0&&parent<s->boneCount&&parent!=b&&parent<VLCR_SKIN_BONES){
  skBoneGlobal(s,parent,local,g,done);
  g[b]=MatrixMultiply(g[parent],local[b]);
 }else g[b]=local[b];
 done[b]=1;
}
static int skBoneCount(const SkSkel*s){
 int n=s?s->boneCount:0;return n>VLCR_SKIN_BONES?VLCR_SKIN_BONES:n;
}
static void skEvalAnim(const SkAnim*ae,float time,Matrix*out){
 const SkSkel*s=ae->skel;int n=skBoneCount(s);
 for(int b=0;b<n;++b){
  const SkTrack*tr=&ae->tracks[b];Vector3 t,sc;Quaternion r;
  skSampleVec(tr->pos,tr->posCount,time,s->bones[b].rt,&t);
  skSampleQuat(tr->rot,tr->rotCount,time,s->bones[b].rr,&r);
  skSampleVec(tr->scale,tr->scaleCount,time,s->bones[b].rs,&sc);
  skLocalTmp[b]=MatrixMultiply(MatrixTranslate(t.x,t.y,t.z),
                               MatrixMultiply(QuaternionToMatrix(r),MatrixScale(sc.x,sc.y,sc.z)));
 }
 memset(skDoneTmp,0,sizeof(unsigned char)*(size_t)VLCR_SKIN_BONES);
 for(int b=0;b<n;++b){
  skBoneGlobal(s,b,skLocalTmp,skGlobalTmp,skDoneTmp);
  out[b]=MatrixMultiply(skGlobalTmp[b],s->bones[b].inv);
 }
}
static void skLocalFromPose(const SkSkel*s,const BonePose*pose,int n,Matrix*local){
 for(int b=0;b<n;++b)
  local[b]=MatrixMultiply(MatrixTranslate(pose[b].position.x,pose[b].position.y,pose[b].position.z),
                          MatrixMultiply(QuaternionToMatrix(pose[b].rotation),
                                         MatrixScale(pose[b].scale.x,pose[b].scale.y,pose[b].scale.z)));
}
static void skSkinFromLocal(const SkSkel*s,const Matrix*local,int n,Matrix*out){
 memset(skDoneTmp,0,sizeof(unsigned char)*(size_t)VLCR_SKIN_BONES);
 for(int b=0;b<n;++b){
  skBoneGlobal(s,b,local,skGlobalTmp,skDoneTmp);
  out[b]=MatrixMultiply(skGlobalTmp[b],s->bones[b].inv);
 }
}
static void skEnsureArena(int n){
 if(skvCap>=n)return;
 SV3*p=(SV3*)realloc(skv,(size_t)n*sizeof(SV3));
 if(!p)return;
 skv=p;skvCap=n;
}
static int skEmit(const SkModel*e){
 int cnt=0;skEnsureArena(VLCR_SKIN_MAX_VERTS);
 if(!skv)return 0;
 for(int i=0;i<e->ic&&cnt<VLCR_SKIN_MAX_VERTS;++i){
  uint32_t ix=e->idx?e->idx[i]:(uint32_t)i;
  if((int)ix>=e->vc)continue;
  const SkinnedVertex3D*s=&e->verts[ix];SV3*d=&skv[cnt++];
  d->x=s->x;d->y=s->y;d->z=s->z;d->nx=s->nx;d->ny=s->ny;d->nz=s->nz;d->u=s->u;d->v=s->v;
  d->tx=s->tx;d->ty=s->ty;d->tz=s->tz;d->tw=s->tw;
  for(int k=0;k<4;++k){d->j[k]=(uint32_t)s->joints[k];d->w[k]=s->weights[k];}
 }
 return cnt;
}
/* Palette matrices are raylib row-major structs; WGSL mat4x4f arrays expect
 * column-major floats, so convert instead of raw-copying the Matrix bytes. */
static void skWritePalette(WGPUBuffer dst,uint64_t offset,const Matrix*pal){
 static float tmp[VLCR_SKIN_BONES*16];
 for(int b=0;b<VLCR_SKIN_BONES;++b)mcm(&pal[b],tmp+b*16);
 wgpuQueueWriteBuffer(g_q,dst,offset,tmp,sizeof tmp);
}
static void skUploadBones(void){
 for(int i=0;i<nskc;++i){
  if(skCmds[i].sid<=0||skCmds[i].sid>=VLCR_SKIN_MODELS)continue;
  skWritePalette(g_skiUBO,(uint64_t)i*VLCR_SKIN_BONE_BYTES,skDrawBones[i]);
 }
}
static void skMaterial(const SkModel*e,Color tint,Color*outBc,float*met,float*rou,unsigned*al,unsigned*mr,unsigned*nm,unsigned*ao,unsigned*em){
 Color bc={255,255,255,255};float m=0,r=.5f;unsigned a=0,t=0,n=0,o=0,emv=0;
 if(e->mat.id>0&&e->mat.id<MA&&mats[e->mat.id].u){
  WA*w=&mats[e->mat.id];bc=w->bc;m=w->met;r=w->rou;a=w->a;t=w->m;n=w->n;o=w->o;emv=w->e;
 }
 bc.r=(unsigned char)((bc.r*tint.r)/255);bc.g=(unsigned char)((bc.g*tint.g)/255);
 bc.b=(unsigned char)((bc.b*tint.b)/255);bc.a=(unsigned char)((bc.a*tint.a)/255);
 *outBc=bc;*met=m;*rou=r;*al=a;*mr=t;*nm=n;*ao=o;*em=emv;
}
static void draw_skinned(WGPURenderPassEncoder pass,int si,uint32_t*uboSend,uint32_t*vbSend){
 if(si<0||si>=nskc||!skModels||!g_pskin||!g_bgSkin||!g_skiVB||!g_skiUBO)return;
 SKC*c=&skCmds[si];
 if(c->sid<=0||c->sid>=VLCR_SKIN_MODELS)return;
 SkModel*e=&skModels[c->sid];
 if(!e->u||!e->verts||!e->skel)return;
 int cnt=skEmit(e);
 if(!cnt)return;
 uint64_t vbytes=(uint64_t)cnt*sizeof(SV3);
 if(*uboSend+UBO_STRIDE>(uint32_t)MAX_DRAWS*UBO_STRIDE)return;
 if((uint64_t)*vbSend+vbytes>(uint64_t)VLCR_SKIN_MAX_VERTS*sizeof(SV3))return;
 Color bc;float met,rou;unsigned al,mr,nm,ao,em;
 skMaterial(e,c->tint,&bc,&met,&rou,&al,&mr,&nm,&ao,&em);
 UBO u;fillu(&u,c->model,bc,met,rou);u.mr[2]=(nm&&nm<MT&&tex[nm].bc5)?1.0f:0.0f;
 uint32_t uboOffset=*uboSend,boff=(uint32_t)si*VLCR_SKIN_BONE_BYTES;
 wgpuQueueWriteBuffer(g_q,g_ubo,uboOffset,&u,sizeof u);
 WGPUBindGroup bg=mkbg(al,mr,nm,ao,em),glob=mkglob(),mat=mkmat(al,mr,nm,ao,em);
 wgpuQueueWriteBuffer(g_q,g_skiVB,*vbSend,skv,(size_t)vbytes);
 wgpuRenderPassEncoderSetPipeline(pass,g_pskin);
 wgpuRenderPassEncoderSetBindGroup(pass,0,bg,1,&uboOffset);
 wgpuRenderPassEncoderSetBindGroup(pass,1,glob,0,NULL);wgpuRenderPassEncoderSetBindGroup(pass,2,mat,0,NULL);
 wgpuRenderPassEncoderSetBindGroup(pass,3,g_bgSkin,1,&boff);
 wgpuRenderPassEncoderSetVertexBuffer(pass,0,g_skiVB,*vbSend,vbytes);
 wgpuRenderPassEncoderDraw(pass,(uint32_t)cnt,1,0,0);
 g_statDraws++;g_statTris+=(uint64_t)(cnt/3);
 *uboSend+=UBO_STRIDE;*vbSend+=(uint32_t)vbytes;
}
static void draw_shadow_skinned(WGPURenderPassEncoder pass,int si,uint32_t*uboSend,uint32_t*vbSend,WGPUBuffer ubo,WGPUBuffer vb,const Matrix*vp){
 if(si<0||si>=nskc||!skModels||!g_pshadowSkin||!g_bgSkin)return;
 SKC*c=&skCmds[si];
 if(c->sid<=0||c->sid>=VLCR_SKIN_MODELS)return;
 SkModel*e=&skModels[c->sid];
 if(!e->u||!e->verts||!e->skel)return;
 int cnt=skEmit(e);
 if(!cnt)return;
 uint64_t vbytes=(uint64_t)cnt*sizeof(SV3);
 if(*uboSend+UBO_STRIDE>(uint32_t)MAX_DRAWS*UBO_STRIDE)return;
 if((uint64_t)*vbSend+vbytes>(uint64_t)MAX_FRAME_VERTS*sizeof(V3))return;
 UBO u;memset(&u,0,sizeof u);mcm(&c->model,u.model);
 Matrix mvp=MatrixMultiply(*vp,c->model);mcm(&mvp,u.mvp);
 uint32_t off=*uboSend,boff=(uint32_t)si*VLCR_SKIN_BONE_BYTES;
 wgpuQueueWriteBuffer(g_q,ubo,off,&u,sizeof u);
 WGPUBindGroup bg=mkbgShadow(ubo);
 wgpuQueueWriteBuffer(g_q,vb,*vbSend,skv,(size_t)vbytes);
 wgpuRenderPassEncoderSetPipeline(pass,g_pshadowSkin);
 wgpuRenderPassEncoderSetBindGroup(pass,0,bg,1,&off);
 wgpuRenderPassEncoderSetBindGroup(pass,1,g_bgSkin,1,&boff);
 wgpuRenderPassEncoderSetVertexBuffer(pass,0,vb,*vbSend,vbytes);
 wgpuRenderPassEncoderDraw(pass,(uint32_t)cnt,1,0,0);
 wgpuBindGroupRelease(bg);g_statShadowDraws++;
 *uboSend+=UBO_STRIDE;*vbSend+=(uint32_t)vbytes;
}
/* Queue a skinned draw; bone matrices are evaluated by the caller. */
static void skQueueDraw(int sid,const Matrix*bones,int boneCount,Matrix model,Color tint){
 if(!bones||sid<=0||sid>=VLCR_SKIN_MODELS||nskc>=VLCR_SKIN_DRAWS)return;
 int slot=nskc;
 memset(skDrawBones[slot],0,sizeof(Matrix)*VLCR_SKIN_BONES);
 for(int b=0;b<boneCount&&b<VLCR_SKIN_BONES;++b)skDrawBones[slot][b]=bones[b];
 skCmds[slot].sid=sid;skCmds[slot].model=model;
 skCmds[slot].tint=tint.a?tint:(Color){255,255,255,255};
 nskc++;
}
static Matrix skModelMatrix(Vector3 p,Vector3 r,Vector3 s){
 return MatrixMultiply(MatrixTranslate(p.x,p.y,p.z),MatrixMultiply(MatrixRotateXYZ(r),MatrixScale(s.x,s.y,s.z)));
}

static Vector3 xf(const Matrix*m,Vector3 p){
 return (Vector3){m->m0*p.x+m->m4*p.y+m->m8*p.z+m->m12,
                  m->m1*p.x+m->m5*p.y+m->m9*p.z+m->m13,
                  m->m2*p.x+m->m6*p.y+m->m10*p.z+m->m14};
}
static Vector3 unproj(const Matrix*m,float x,float y,float z){
 float X=m->m0*x+m->m4*y+m->m8*z+m->m12;
 float Y=m->m1*x+m->m5*y+m->m9*z+m->m13;
 float Z=m->m2*x+m->m6*y+m->m10*z+m->m14;
 float W=m->m3*x+m->m7*y+m->m11*z+m->m15;
 if(fabsf(W)<1e-6f)W=1.0f;return (Vector3){X/W,Y/W,Z/W};
}
static void computeShadowMatrix(void){
 if(!g_shadowEnabled||nc<=0)return;
 Matrix vp=MatrixMultiply(g_proj,g_view);Matrix inv=MatrixInvert(vp);
 Vector3 d=g_ld;float dl=sqrtf(d.x*d.x+d.y*d.y+d.z*d.z);
 if(dl<1e-6f)d=(Vector3){-0.4f,-0.7f,-0.3f};else{d.x/=dl;d.y/=dl;d.z/=dl;}
 float A=g_proj.m10,B=g_proj.m14;float zn=B/A,zf=B/(1.0f+A);
 if(!(zn>0.001f)||!(zf>zn)){zn=0.1f;zf=100.0f;}
 float ndc[5]={0,0,0,0,0},splits[5]={zn,zn,zn,zn,zf};
 for(int i=1;i<=4;++i){float t=(float)i*0.25f;float ls=zn*powf(zf/zn,t);float us=zn+(zf-zn)*t;
  float s=0.7f*ls+0.3f*us;splits[i]=s;ndc[i]=(-A*s+B)/s;}
 Matrix vps[4];
 for(int ci=0;ci<4;++ci){
  Vector3 corners[8];int k=0;
  for(int zi=0;zi<2;++zi){float z=ndc[ci+zi];
   for(int sx=-1;sx<=1;sx+=2)for(int sy=-1;sy<=1;sy+=2)corners[k++]=unproj(&inv,(float)sx,(float)sy,z);}
  Vector3 center={0,0,0};for(int i=0;i<8;++i){center.x+=corners[i].x;center.y+=corners[i].y;center.z+=corners[i].z;}
  center.x*=0.125f;center.y*=0.125f;center.z*=0.125f;
  float maxd=1.0f;for(int i=0;i<8;++i){Vector3 q=Vector3Subtract(corners[i],center);float l=Vector3Length(q);if(l>maxd)maxd=l;}
  Vector3 eye={center.x-d.x*(maxd+5),center.y-d.y*(maxd+5),center.z-d.z*(maxd+5)};
  Matrix lv=MatrixLookAt(eye,center,(Vector3){0,1,0});
  Vector3 mn={1e30f,1e30f,1e30f},mx={-1e30f,-1e30f,-1e30f};
  for(int i=0;i<8;++i){Vector3 q=xf(&lv,corners[i]);
   if(q.x<mn.x)mn.x=q.x;if(q.x>mx.x)mx.x=q.x;if(q.y<mn.y)mn.y=q.y;if(q.y>mx.y)mx.y=q.y;if(q.z<mn.z)mn.z=q.z;if(q.z>mx.z)mx.z=q.z;}
  float w=mx.x-mn.x,h=mx.y-mn.y;if(w<0.1f)w=0.1f;if(h<0.1f)h=0.1f;
  float tx=w/1024.0f,ty=h/1024.0f;float cx=(mn.x+mx.x)*0.5f,cy=(mn.y+mx.y)*0.5f;
  if(tx>1e-6f)cx=floorf(cx/tx+0.5f)*tx;if(ty>1e-6f)cy=floorf(cy/ty+0.5f)*ty;
  float hx=w*0.5f+0.5f,hy=h*0.5f+0.5f;
  Matrix proj=MatrixOrtho(cx-hx,cx+hx,cy-hy,cy+hy,0.1f,maxd*2.0f+20.0f);
  vps[ci]=MatrixMultiply(proj,lv);
 }
 g_shadowSplits[0]=splits[1];g_shadowSplits[1]=splits[2];g_shadowSplits[2]=splits[3];g_shadowSplits[3]=1e9f;
 g_shadowVP=vps[0];g_shadowVP2=vps[1];g_shadowVP3=vps[2];g_shadowVP4=vps[3];
 g_shadowValid=1;
}
static void flush_batch(WGPURenderPassEncoder pass,uint32_t*uboSend,uint32_t*vbSend){
 if(n3>0){
  uint64_t vbytes=(uint64_t)n3*sizeof(V3);
  if(*uboSend+UBO_STRIDE<=(uint32_t)MAX_DRAWS*UBO_STRIDE&&(uint64_t)*vbSend+vbytes<=(uint64_t)MAX_FRAME_VERTS*sizeof(V3)){
   UBO u;fillu(&u,MatrixIdentity(),(Color){255,255,255,255},0,.5f);
   uint32_t uboOffset=*uboSend;
   wgpuQueueWriteBuffer(g_q,g_ubo,uboOffset,&u,sizeof u);
   WGPUBindGroup bg=mkbg(0,0,0,0,0),glob=mkglob(),mat=mkmat(0,0,0,0,0);wgpuQueueWriteBuffer(g_q,g_vb3,*vbSend,b3,(size_t)vbytes);
   wgpuRenderPassEncoderSetPipeline(pass,g_pbr);wgpuRenderPassEncoderSetBindGroup(pass,0,bg,1,&uboOffset);
   wgpuRenderPassEncoderSetBindGroup(pass,1,glob,0,NULL);wgpuRenderPassEncoderSetBindGroup(pass,2,mat,0,NULL);
   wgpuRenderPassEncoderSetVertexBuffer(pass,0,g_vb3,*vbSend,vbytes);wgpuRenderPassEncoderDraw(pass,(uint32_t)n3,1,0,0);g_statDraws++;g_statTris+=(uint64_t)(n3/3);
   *uboSend+=UBO_STRIDE; *vbSend+=(uint32_t)vbytes;
  }
  n3=0;
 }
 if(nl>0){
  uint64_t vbytes=(uint64_t)nl*sizeof(V3);
  if(*uboSend+UBO_STRIDE<=(uint32_t)MAX_DRAWS*UBO_STRIDE&&(uint64_t)*vbSend+vbytes<=(uint64_t)MAX_FRAME_VERTS*sizeof(V3)){
   UBO u;fillu(&u,MatrixIdentity(),(Color){255,255,255,255},0,1);
   uint32_t uboOffset=*uboSend;
   wgpuQueueWriteBuffer(g_q,g_ubo,uboOffset,&u,sizeof u);
   WGPUBindGroup bg=mkbg(0,0,0,0,0);wgpuQueueWriteBuffer(g_q,g_vb3,*vbSend,bl,(size_t)vbytes);
   wgpuRenderPassEncoderSetPipeline(pass,g_plines);wgpuRenderPassEncoderSetBindGroup(pass,0,bg,1,&uboOffset);
   wgpuRenderPassEncoderSetVertexBuffer(pass,0,g_vb3,*vbSend,vbytes);wgpuRenderPassEncoderDraw(pass,(uint32_t)nl,1,0,0);g_statDraws++;
   *uboSend+=UBO_STRIDE; *vbSend+=(uint32_t)vbytes;
  }
  nl=0;
 }
 if(n2>0||nb2c>0){
  wgpuQueueWriteBuffer(g_q,g_vb2,0,b2,(size_t)n2*sizeof(V2));
  wgpuRenderPassEncoderSetPipeline(pass,g_p2);
  wgpuRenderPassEncoderSetVertexBuffer(pass,0,g_vb2,0,(uint64_t)(n2>0?n2:1)*sizeof(V2));
  for(int i=0;i<nb2c;++i){
    B2Cmd*c=&b2cmds[i];
    WGPUBindGroupEntry e[2]={{0}};e[0].binding=0;e[0].sampler=(c->texId&&c->texId<MT&&tex[c->texId].samp)?tex[c->texId].samp:g_samp;e[1].binding=1;e[1].textureView=tview(c->texId);
    WGPUBindGroupDescriptor d={0};d.layout=g_bgl2;d.entryCount=2;d.entries=e;
    WGPUBindGroup bg=wgpuDeviceCreateBindGroup(g_dev,&d);
    wgpuRenderPassEncoderSetBindGroup(pass,0,bg,0,NULL);
    wgpuRenderPassEncoderDraw(pass,(uint32_t)c->count,1,(uint32_t)c->start,0);g_statDraws++;
    wgpuBindGroupRelease(bg);
  }
  n2=0;nb2c=0;
 }
}
typedef struct PostCtx{Matrix taaVP;WGPUBindGroup taaComp;WGPUTextureView target;int taaActive;}PostCtx;
static void postSsao(WGPUCommandEncoder enc,PostCtx*c){
 if(c->taaActive||!g_ssaoEnabled||!g_pssao||!g_bgSsao||!g_ssaoView)return;
 SSAOUBO su;Matrix ip=MatrixInvert(g_proj);mcm(&ip,su.invProj);su.params[0]=g_ssaoRadius;su.params[1]=g_ssaoIntensity;
 su.params[2]=1.0f;su.params[3]=0.0f;wgpuQueueWriteBuffer(g_q,g_ssaoUBO,0,&su,sizeof su);
 WGPURenderPassColorAttachment aa={0};aa.view=g_ssaoView;aa.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;
 aa.loadOp=WGPULoadOp_Clear;aa.storeOp=WGPUStoreOp_Store;aa.clearValue.r=1;aa.clearValue.g=1;aa.clearValue.b=1;aa.clearValue.a=1;
 WGPURenderPassDescriptor arp={0};arp.colorAttachmentCount=1;arp.colorAttachments=&aa;
 WGPURenderPassEncoder ap=wgpuCommandEncoderBeginRenderPass(enc,&arp);
 wgpuRenderPassEncoderSetPipeline(ap,g_pssao);wgpuRenderPassEncoderSetBindGroup(ap,0,g_bgSsao,0,NULL);
 wgpuRenderPassEncoderDraw(ap,3,1,0,0);wgpuRenderPassEncoderEnd(ap);wgpuRenderPassEncoderRelease(ap);
}
static void postBloom(WGPUCommandEncoder enc,PostCtx*c){
 if(c->taaActive||!g_bloomEnabled||!g_pbright||!g_bgc||!g_bloomView)return;
 WGPURenderPassColorAttachment bca={0};bca.view=g_bloomView;bca.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;
 bca.loadOp=WGPULoadOp_Clear;bca.storeOp=WGPUStoreOp_Store;bca.clearValue.r=0;bca.clearValue.g=0;bca.clearValue.b=0;bca.clearValue.a=1;
 WGPURenderPassDescriptor brp={0};brp.colorAttachmentCount=1;brp.colorAttachments=&bca;
 WGPURenderPassEncoder bp=wgpuCommandEncoderBeginRenderPass(enc,&brp);
 wgpuRenderPassEncoderSetPipeline(bp,g_pbright);wgpuRenderPassEncoderSetBindGroup(bp,0,g_bgc,0,NULL);
 wgpuRenderPassEncoderDraw(bp,3,1,0,0);wgpuRenderPassEncoderEnd(bp);wgpuRenderPassEncoderRelease(bp);
}
static void postVelocity(WGPUCommandEncoder enc,PostCtx*c){
 if(!c->taaActive||!g_pVel||!g_velUBO||!g_velVB||!g_velView||!g_bglVel)return;
 WGPURenderPassColorAttachment vca={0};vca.view=g_velView;vca.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;
 vca.loadOp=WGPULoadOp_Clear;vca.storeOp=WGPUStoreOp_Store;vca.clearValue.r=0;vca.clearValue.g=0;vca.clearValue.b=0;vca.clearValue.a=1;
 WGPURenderPassDescriptor vrp={0};vrp.colorAttachmentCount=1;vrp.colorAttachments=&vca;
 WGPURenderPassEncoder vp=wgpuCommandEncoderBeginRenderPass(enc,&vrp);
 uint32_t vu=0,vv=0;Matrix pvp=(g_taaFrame==0)?c->taaVP:g_prevVP;
 for(int i=0;i<nc;++i){
  DC*cc=&cmds[i];if(cc->mid<=0||cc->mid>=MM||!meshes[cc->mid].u)continue;
  WM*m=&meshes[cc->mid];int cnt=0;
  for(int k=0;k<m->ic&&cnt<65536;++k){uint32_t ix=m->i?m->i[k]:(uint32_t)k;if((int)ix>=m->vc)continue;b3[cnt++]=m->v[ix];}
  if(!cnt)continue;uint64_t vbytes=(uint64_t)cnt*sizeof(V3);
  if(vu+256>(uint32_t)MAX_DRAWS*256||(uint64_t)vv+vbytes>(uint64_t)MAX_FRAME_VERTS*sizeof(V3))break;
  VELUBO u;mcm(&c->taaVP,u.curVP);mcm(&pvp,u.prevVP);mcm(&cc->model,u.curModel);
  Matrix pm=(g_taaFrame==0||!g_prevCmdValid[i]||g_prevCmdMid[i]!=cc->mid)?cc->model:g_prevCmdModel[i];mcm(&pm,u.prevModel);
  wgpuQueueWriteBuffer(g_q,g_velUBO,vu,&u,sizeof u);wgpuQueueWriteBuffer(g_q,g_velVB,vv,b3,(size_t)vbytes);
  WGPUBindGroupEntry ue={0};ue.binding=0;ue.buffer=g_velUBO;ue.size=sizeof(VELUBO);
  WGPUBindGroupDescriptor ud={0};ud.layout=g_bglVel;ud.entryCount=1;ud.entries=&ue;
  WGPUBindGroup bg=wgpuDeviceCreateBindGroup(g_dev,&ud);
  wgpuRenderPassEncoderSetPipeline(vp,g_pVel);wgpuRenderPassEncoderSetBindGroup(vp,0,bg,1,&vu);
  wgpuRenderPassEncoderSetVertexBuffer(vp,0,g_velVB,vv,vbytes);wgpuRenderPassEncoderDraw(vp,(uint32_t)cnt,1,0,0);
  wgpuBindGroupRelease(bg);vu+=256;vv+=(uint32_t)vbytes;
 }
 if(g_pVelSkin&&g_bgSkinVel)for(int i=0;i<nskc;++i){
  SKC*cc=&skCmds[i];if(cc->sid<=0||cc->sid>=VLCR_SKIN_MODELS)continue;
  SkModel*e=&skModels[cc->sid];if(!e->u||!e->skel)continue;
  int cnt=skEmit(e);if(!cnt)continue;
  uint64_t vbytes=(uint64_t)cnt*sizeof(SV3);
  if(vu+256>(uint32_t)MAX_DRAWS*256||(uint64_t)vv+vbytes>(uint64_t)VLCR_SKIN_MAX_VERTS*sizeof(SV3))break;
  VELUBO u;mcm(&c->taaVP,u.curVP);mcm(&pvp,u.prevVP);mcm(&cc->model,u.curModel);
  Matrix pm=(g_taaFrame==0||!e->prevValid)?cc->model:e->prevModel;mcm(&pm,u.prevModel);
  const Matrix*prevBones=e->prevValid?e->prevBones:skDrawBones[i];
  uint32_t boff=(uint32_t)i*VLCR_SKIN_CURPREV_BYTES;
  wgpuQueueWriteBuffer(g_q,g_velUBO,vu,&u,sizeof u);
  skWritePalette(g_velSkUBO,boff,skDrawBones[i]);
  skWritePalette(g_velSkUBO,boff+VLCR_SKIN_BONE_BYTES,prevBones);
  wgpuQueueWriteBuffer(g_q,g_velSkVB,vv,skv,(size_t)vbytes);
  WGPUBindGroupEntry ue={0};ue.binding=0;ue.buffer=g_velUBO;ue.size=sizeof(VELUBO);
  WGPUBindGroupDescriptor ud={0};ud.layout=g_bglVel;ud.entryCount=1;ud.entries=&ue;
  WGPUBindGroup bg=wgpuDeviceCreateBindGroup(g_dev,&ud);
  wgpuRenderPassEncoderSetPipeline(vp,g_pVelSkin);wgpuRenderPassEncoderSetBindGroup(vp,0,bg,1,&vu);
  wgpuRenderPassEncoderSetBindGroup(vp,1,g_bgSkinVel,1,&boff);
  wgpuRenderPassEncoderSetVertexBuffer(vp,0,g_velSkVB,vv,vbytes);
  wgpuRenderPassEncoderDraw(vp,(uint32_t)cnt,1,0,0);
  wgpuBindGroupRelease(bg);vu+=256;vv+=(uint32_t)vbytes;
 }
 wgpuRenderPassEncoderEnd(vp);wgpuRenderPassEncoderRelease(vp);
 for(int i=0;i<nc;++i){g_prevCmdModel[i]=cmds[i].model;g_prevCmdMid[i]=cmds[i].mid;g_prevCmdValid[i]=1;}
}
static void postTaa(WGPUCommandEncoder enc,PostCtx*c){
 if(!c->taaActive||!g_pTaa||!g_taaUBO||!g_bglc||!g_samp||!g_depthV)return;
 TAAUBO tu;mcm(&g_prevVP,tu.prevVP);Matrix ivp=MatrixInvert(c->taaVP);mcm(&ivp,tu.invVP);
 tu.params[0]=0.9f;tu.params[1]=0;tu.params[2]=(g_taaFrame==0)?1.0f:0.0f;tu.params[3]=0;
 wgpuQueueWriteBuffer(g_q,g_taaUBO,0,&tu,sizeof tu);
 WGPUTextureView taaOut=g_taaView[g_taaCur];
 WGPUBindGroupEntry te[6]={{0}};te[0].binding=0;te[0].buffer=g_taaUBO;te[0].size=sizeof(TAAUBO);
 te[1].binding=1;te[1].textureView=g_sceneView;te[2].binding=2;te[2].textureView=g_depthV;
 te[3].binding=3;te[3].textureView=g_taaView[1-g_taaCur];te[4].binding=4;te[4].sampler=g_samp;
 te[5].binding=5;te[5].textureView=g_velView;
 WGPUBindGroupDescriptor td={0};td.layout=g_bglTaa;td.entryCount=6;td.entries=te;
 WGPUBindGroup tbg=wgpuDeviceCreateBindGroup(g_dev,&td);
 WGPURenderPassColorAttachment tca={0};tca.view=taaOut;tca.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;
 tca.loadOp=WGPULoadOp_Clear;tca.storeOp=WGPUStoreOp_Store;tca.clearValue.r=0;tca.clearValue.g=0;tca.clearValue.b=0;tca.clearValue.a=1;
 WGPURenderPassDescriptor trp={0};trp.colorAttachmentCount=1;trp.colorAttachments=&tca;
 WGPURenderPassEncoder tp=wgpuCommandEncoderBeginRenderPass(enc,&trp);
 wgpuRenderPassEncoderSetPipeline(tp,g_pTaa);wgpuRenderPassEncoderSetBindGroup(tp,0,tbg,0,NULL);
 wgpuRenderPassEncoderDraw(tp,3,1,0,0);wgpuRenderPassEncoderEnd(tp);wgpuRenderPassEncoderRelease(tp);
 wgpuBindGroupRelease(tbg);
 WGPUBindGroupEntry ce[2]={{0}};ce[0].binding=0;ce[0].sampler=g_samp;ce[1].binding=1;ce[1].textureView=taaOut;
 WGPUBindGroupDescriptor cd={0};cd.layout=g_bglc;cd.entryCount=2;cd.entries=ce;
 c->taaComp=wgpuDeviceCreateBindGroup(g_dev,&cd);
}
static void postComposite(WGPUCommandEncoder enc,PostCtx*c){
 WGPURenderPassColorAttachment ca={0};ca.view=c->target;ca.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;ca.loadOp=WGPULoadOp_Clear;ca.storeOp=WGPUStoreOp_Store;
 ca.clearValue.r=0;ca.clearValue.g=0;ca.clearValue.b=0;ca.clearValue.a=1;
 if(!ca.view)return;
 WGPURenderPassDescriptor crp={0};crp.colorAttachmentCount=1;crp.colorAttachments=&ca;
 WGPURenderPassEncoder cp=wgpuCommandEncoderBeginRenderPass(enc,&crp);
 WGPURenderPipeline cpip=0;WGPUBindGroup cbg=0;
 if(c->taaComp){cpip=(g_fxaa&&g_pcompFxaa)?g_pcompFxaa:g_pcomp;cbg=c->taaComp;}
 else if(g_ssaoEnabled&&g_pcompSsao&&g_bgSsaoComp){cpip=g_pcompSsao;cbg=g_bgSsaoComp;}
 else if(g_bloomEnabled&&g_pcompBloom&&g_bgBloom){cpip=g_pcompBloom;cbg=g_bgBloom;}
 else if(g_bgc){cpip=(g_fxaa&&g_pcompFxaa)?g_pcompFxaa:g_pcomp;cbg=g_bgc;}
 if(cpip&&cbg){wgpuRenderPassEncoderSetPipeline(cp,cpip);wgpuRenderPassEncoderSetBindGroup(cp,0,cbg,0,NULL);
  wgpuRenderPassEncoderDraw(cp,3,1,0,0);}
 wgpuRenderPassEncoderEnd(cp);wgpuRenderPassEncoderRelease(cp);
}
static void postDispatch(WGPUCommandEncoder enc,int pass,void*user){
 PostCtx*c=(PostCtx*)user;
 switch(pass){
  case PG_SSAO:postSsao(enc,c);break;
  case PG_BLOOM:postBloom(enc,c);break;
  case PG_VELOCITY:postVelocity(enc,c);break;
  case PG_TAA:postTaa(enc,c);break;
  case PG_COMPOSITE:postComposite(enc,c);break;
  default:break;
 }
}
void EndVulkanFrame(void){
 if(!g_ok||!g_surf||!g_swapchain||!g_sceneView)return;
 WGPUTextureView view=wgpuSwapChainGetCurrentTextureView(g_swapchain);
 if(!view){cfg();return;}
 WGPUCommandEncoder enc=wgpuDeviceCreateCommandEncoder(g_dev,NULL);
 if(g_tsSet)wgpuCommandEncoderWriteTimestamp(enc,g_tsSet,0);
 Matrix baseProj=g_proj;PostCtx ctx;memset(&ctx,0,sizeof ctx);ctx.taaVP=baseProj;int taaActive=0;
 if(g_taaEnabled&&g_taaView[0]){
  static const float jx[8]={0.0f,0.5f,-0.5f,0.25f,0.75f,-0.25f,0.125f,-0.75f};
  static const float jy[8]={0.0f,0.5f,0.25f,-0.5f,0.125f,0.75f,-0.25f,-0.125f};
  int j=g_taaFrame&7;float w=gw>0?(float)gw:1.0f,h=gh>0?(float)gh:1.0f;
  g_proj.m12+=jx[j]*2.0f/w;g_proj.m13+=jy[j]*2.0f/h;
  ctx.taaVP=MatrixMultiply(g_proj,g_view);taaActive=1;
 }
 ctx.taaActive=taaActive;ctx.target=g_finalView?g_finalView:view;

 computeShadowMatrix();
 skUploadBones();
 if(g_shadowEnabled&&g_shadowValid&&g_shadowView){
  WGPURenderPassDepthStencilAttachment sda={0};sda.view=g_shadowView;sda.depthLoadOp=WGPULoadOp_Clear;
  sda.depthStoreOp=WGPUStoreOp_Store;sda.depthClearValue=1;
  WGPURenderPassDescriptor srp={0};srp.colorAttachmentCount=0;srp.colorAttachments=NULL;srp.depthStencilAttachment=&sda;
  WGPURenderPassEncoder sp=wgpuCommandEncoderBeginRenderPass(enc,&srp);
  uint32_t su=0,sv=0;
  wgpuRenderPassEncoderSetViewport(sp,0,0,1024,1024,0,1);
  for(int i=0;i<nc;++i)draw_shadow(sp,&cmds[i],&su,&sv,g_shadowUBO,g_shadowVB,&g_shadowVP);
  for(int i=0;i<nskc;++i)draw_shadow_skinned(sp,i,&su,&sv,g_shadowUBO,g_shadowVB,&g_shadowVP);
  su=sv=0;wgpuRenderPassEncoderSetViewport(sp,1024,0,1024,1024,0,1);
  for(int i=0;i<nc;++i)draw_shadow(sp,&cmds[i],&su,&sv,g_shadowUBO2,g_shadowVB2,&g_shadowVP2);
  for(int i=0;i<nskc;++i)draw_shadow_skinned(sp,i,&su,&sv,g_shadowUBO2,g_shadowVB2,&g_shadowVP2);
  su=sv=0;wgpuRenderPassEncoderSetViewport(sp,0,1024,1024,1024,0,1);
  for(int i=0;i<nc;++i)draw_shadow(sp,&cmds[i],&su,&sv,g_shadowUBO3,g_shadowVB3,&g_shadowVP3);
  for(int i=0;i<nskc;++i)draw_shadow_skinned(sp,i,&su,&sv,g_shadowUBO3,g_shadowVB3,&g_shadowVP3);
  su=sv=0;wgpuRenderPassEncoderSetViewport(sp,1024,1024,1024,1024,0,1);
  for(int i=0;i<nc;++i)draw_shadow(sp,&cmds[i],&su,&sv,g_shadowUBO4,g_shadowVB4,&g_shadowVP4);
  for(int i=0;i<nskc;++i)draw_shadow_skinned(sp,i,&su,&sv,g_shadowUBO4,g_shadowVB4,&g_shadowVP4);
  wgpuRenderPassEncoderEnd(sp);wgpuRenderPassEncoderRelease(sp);
 }
 if(g_spotOn&&g_spotView&&g_spotUBO&&g_spotVB){
  WGPURenderPassDepthStencilAttachment sda={0};sda.view=g_spotView;sda.depthLoadOp=WGPULoadOp_Clear;
  sda.depthStoreOp=WGPUStoreOp_Store;sda.depthClearValue=1;
  WGPURenderPassDescriptor srp={0};srp.colorAttachmentCount=0;srp.colorAttachments=NULL;srp.depthStencilAttachment=&sda;
  WGPURenderPassEncoder sp=wgpuCommandEncoderBeginRenderPass(enc,&srp);
  uint32_t su=0,sv=0;wgpuRenderPassEncoderSetViewport(sp,0,0,1024,1024,0,1);
  for(int i=0;i<nc;++i)draw_shadow(sp,&cmds[i],&su,&sv,g_spotUBO,g_spotVB,&g_spotVP);
  for(int i=0;i<nskc;++i)draw_shadow_skinned(sp,i,&su,&sv,g_spotUBO,g_spotVB,&g_spotVP);
  wgpuRenderPassEncoderEnd(sp);wgpuRenderPassEncoderRelease(sp);
 }
 if(g_extraSpots[0].on&&g_spot2Tex){
  WGPURenderPassDepthStencilAttachment sda={0};sda.view=g_spot2View;sda.depthLoadOp=WGPULoadOp_Clear;
  sda.depthStoreOp=WGPUStoreOp_Store;sda.depthClearValue=1;
  WGPURenderPassDescriptor srp={0};srp.colorAttachmentCount=0;srp.colorAttachments=NULL;srp.depthStencilAttachment=&sda;
  WGPURenderPassEncoder sp=wgpuCommandEncoderBeginRenderPass(enc,&srp);
  uint32_t su=0,sv=0;wgpuRenderPassEncoderSetViewport(sp,0,0,1024,1024,0,1);
  for(int i=0;i<nc;++i)draw_shadow(sp,&cmds[i],&su,&sv,g_spot2UBO,g_spot2VB,&g_spot2VP);
  for(int i=0;i<nskc;++i)draw_shadow_skinned(sp,i,&su,&sv,g_spot2UBO,g_spot2VB,&g_spot2VP);
  wgpuRenderPassEncoderEnd(sp);wgpuRenderPassEncoderRelease(sp);
 }
 if(g_extraSpots[1].on&&g_spot3Tex){
  WGPURenderPassDepthStencilAttachment sda={0};sda.view=g_spot3View;sda.depthLoadOp=WGPULoadOp_Clear;
  sda.depthStoreOp=WGPUStoreOp_Store;sda.depthClearValue=1;
  WGPURenderPassDescriptor srp={0};srp.colorAttachmentCount=0;srp.colorAttachments=NULL;srp.depthStencilAttachment=&sda;
  WGPURenderPassEncoder sp=wgpuCommandEncoderBeginRenderPass(enc,&srp);
  uint32_t su=0,sv=0;wgpuRenderPassEncoderSetViewport(sp,0,0,1024,1024,0,1);
  for(int i=0;i<nc;++i)draw_shadow(sp,&cmds[i],&su,&sv,g_spot3UBO,g_spot3VB,&g_spot3VP);
  for(int i=0;i<nskc;++i)draw_shadow_skinned(sp,i,&su,&sv,g_spot3UBO,g_spot3VB,&g_spot3VP);
  wgpuRenderPassEncoderEnd(sp);wgpuRenderPassEncoderRelease(sp);
 }
 if(g_extraSpots[2].on&&g_spot4Tex){
  WGPURenderPassDepthStencilAttachment sda={0};sda.view=g_spot4View;sda.depthLoadOp=WGPULoadOp_Clear;
  sda.depthStoreOp=WGPUStoreOp_Store;sda.depthClearValue=1;
  WGPURenderPassDescriptor srp={0};srp.colorAttachmentCount=0;srp.colorAttachments=NULL;srp.depthStencilAttachment=&sda;
  WGPURenderPassEncoder sp=wgpuCommandEncoderBeginRenderPass(enc,&srp);
  uint32_t su=0,sv=0;wgpuRenderPassEncoderSetViewport(sp,0,0,1024,1024,0,1);
  for(int i=0;i<nc;++i)draw_shadow(sp,&cmds[i],&su,&sv,g_spot4UBO,g_spot4VB,&g_spot4VP);
  for(int i=0;i<nskc;++i)draw_shadow_skinned(sp,i,&su,&sv,g_spot4UBO,g_spot4VB,&g_spot4VP);
  wgpuRenderPassEncoderEnd(sp);wgpuRenderPassEncoderRelease(sp);
 }
 if(g_points[0].on&&g_pointTex){
  for(int f=0;f<6;++f){
   WGPURenderPassDepthStencilAttachment sda={0};sda.view=g_pointArrFace[0][f];sda.depthLoadOp=WGPULoadOp_Clear;
   sda.depthStoreOp=WGPUStoreOp_Store;sda.depthClearValue=1;
   WGPURenderPassDescriptor srp={0};srp.colorAttachmentCount=0;srp.colorAttachments=NULL;srp.depthStencilAttachment=&sda;
   WGPURenderPassEncoder sp=wgpuCommandEncoderBeginRenderPass(enc,&srp);
   uint32_t su=0,sv=0;wgpuRenderPassEncoderSetViewport(sp,0,0,512,512,0,1);
   for(int i=0;i<nc;++i)draw_shadow(sp,&cmds[i],&su,&sv,g_pointUBO[f],g_pointVB[f],&g_pointVP[f]);
   for(int i=0;i<nskc;++i)draw_shadow_skinned(sp,i,&su,&sv,g_pointUBO[f],g_pointVB[f],&g_pointVP[f]);
   wgpuRenderPassEncoderEnd(sp);wgpuRenderPassEncoderRelease(sp);
  }
 }
 if(g_points[1].on&&g_point2Tex){
  for(int f=0;f<6;++f){
   WGPURenderPassDepthStencilAttachment sda={0};sda.view=g_pointArrFace[1][f];sda.depthLoadOp=WGPULoadOp_Clear;
   sda.depthStoreOp=WGPUStoreOp_Store;sda.depthClearValue=1;
   WGPURenderPassDescriptor srp={0};srp.colorAttachmentCount=0;srp.colorAttachments=NULL;srp.depthStencilAttachment=&sda;
   WGPURenderPassEncoder sp=wgpuCommandEncoderBeginRenderPass(enc,&srp);
   uint32_t su=0,sv=0;wgpuRenderPassEncoderSetViewport(sp,0,0,512,512,0,1);
   for(int i=0;i<nc;++i)draw_shadow(sp,&cmds[i],&su,&sv,g_point2UBO[f],g_point2VB[f],&g_point2VP[f]);
   for(int i=0;i<nskc;++i)draw_shadow_skinned(sp,i,&su,&sv,g_point2UBO[f],g_point2VB[f],&g_point2VP[f]);
   wgpuRenderPassEncoderEnd(sp);wgpuRenderPassEncoderRelease(sp);
  }
 }
 if(g_points[2].on&&g_point3Tex){
  for(int f=0;f<6;++f){
   WGPURenderPassDepthStencilAttachment sda={0};sda.view=g_pointArrFace[2][f];sda.depthLoadOp=WGPULoadOp_Clear;
   sda.depthStoreOp=WGPUStoreOp_Store;sda.depthClearValue=1;
   WGPURenderPassDescriptor srp={0};srp.colorAttachmentCount=0;srp.colorAttachments=NULL;srp.depthStencilAttachment=&sda;
   WGPURenderPassEncoder sp=wgpuCommandEncoderBeginRenderPass(enc,&srp);
   uint32_t su=0,sv=0;wgpuRenderPassEncoderSetViewport(sp,0,0,512,512,0,1);
   for(int i=0;i<nc;++i)draw_shadow(sp,&cmds[i],&su,&sv,g_point3UBO[f],g_point3VB[f],&g_point3VP[f]);
   for(int i=0;i<nskc;++i)draw_shadow_skinned(sp,i,&su,&sv,g_point3UBO[f],g_point3VB[f],&g_point3VP[f]);
   wgpuRenderPassEncoderEnd(sp);wgpuRenderPassEncoderRelease(sp);
  }
 }
 if(g_points[3].on&&g_pointArrTex){
  for(int f=0;f<6;++f){
   WGPURenderPassDepthStencilAttachment sda={0};sda.view=g_pointArrFace[3][f];sda.depthLoadOp=WGPULoadOp_Clear;
   sda.depthStoreOp=WGPUStoreOp_Store;sda.depthClearValue=1;
   WGPURenderPassDescriptor srp={0};srp.colorAttachmentCount=0;srp.colorAttachments=NULL;srp.depthStencilAttachment=&sda;
   WGPURenderPassEncoder sp=wgpuCommandEncoderBeginRenderPass(enc,&srp);
   uint32_t su=0,sv=0;wgpuRenderPassEncoderSetViewport(sp,0,0,512,512,0,1);
   for(int i=0;i<nc;++i)draw_shadow(sp,&cmds[i],&su,&sv,g_point3UBO[f],g_point3VB[f],&g_point4VP[f]);
   for(int i=0;i<nskc;++i)draw_shadow_skinned(sp,i,&su,&sv,g_point3UBO[f],g_point3VB[f],&g_point4VP[f]);
   wgpuRenderPassEncoderEnd(sp);wgpuRenderPassEncoderRelease(sp);
  }
 }

 /* Scene pass Р Р†РІР‚В РІР‚в„ў offscreen scene color/depth. */
 WGPURenderPassColorAttachment sca={0};sca.view=g_sceneView;sca.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;sca.loadOp=WGPULoadOp_Clear;sca.storeOp=WGPUStoreOp_Store;
 sca.clearValue.r=g_clear[0];sca.clearValue.g=g_clear[1];sca.clearValue.b=g_clear[2];sca.clearValue.a=g_clear[3];
 WGPURenderPassDepthStencilAttachment da={0};da.view=g_depthV;da.depthLoadOp=WGPULoadOp_Clear;da.depthStoreOp=WGPUStoreOp_Store;da.depthClearValue=1;
 WGPURenderPassDescriptor srp={0};srp.colorAttachmentCount=1;srp.colorAttachments=&sca;if(g_depthV)srp.depthStencilAttachment=&da;
 WGPURenderPassEncoder pass=wgpuCommandEncoderBeginRenderPass(enc,&srp);
 uint32_t uboSend=0,vbSend=0;
 for(int i=0;i<nc;++i)draw_cmd(pass,&cmds[i],&uboSend,&vbSend);
 for(int i=0;i<nskc;++i)draw_skinned(pass,i,&uboSend,&vbSend);
 flush_batch(pass,&uboSend,&vbSend);
 wgpuRenderPassEncoderEnd(pass);

 /* SSAO, bloom, velocity, TAA and composite are ordered by the render graph
    from their read/write declarations; the graph owns the SSAO/bloom targets. */
 if(!g_postGraph||!vrgwExecute(g_postGraph,enc,postDispatch,&ctx)){
  postSsao(enc,&ctx);postBloom(enc,&ctx);postVelocity(enc,&ctx);
  postTaa(enc,&ctx);postComposite(enc,&ctx);
 }
 for(int i=0;i<nskc;++i){
  int sid=skCmds[i].sid;if(sid<=0||sid>=VLCR_SKIN_MODELS)continue;
  SkModel*e=&skModels[sid];if(!e->u)continue;
  memcpy(e->prevBones,skDrawBones[i],sizeof(Matrix)*VLCR_SKIN_BONES);
  e->prevModel=skCmds[i].model;e->prevValid=1;
 }

 /* Blit the final composite texture to the canvas (already display-encoded,
    so this must stay a plain copy -- never the tonemapping composite). */
 if(g_finalView&&g_bgFinal&&g_pblit){
  WGPURenderPassColorAttachment fca={0};fca.view=view;fca.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;
  fca.loadOp=WGPULoadOp_Clear;fca.storeOp=WGPUStoreOp_Store;fca.clearValue.r=0;fca.clearValue.g=0;fca.clearValue.b=0;fca.clearValue.a=1;
  WGPURenderPassDescriptor frp={0};frp.colorAttachmentCount=1;frp.colorAttachments=&fca;
  WGPURenderPassEncoder fp=wgpuCommandEncoderBeginRenderPass(enc,&frp);
  wgpuRenderPassEncoderSetPipeline(fp,g_pblit);wgpuRenderPassEncoderSetBindGroup(fp,0,g_bgFinal,0,NULL);
  wgpuRenderPassEncoderDraw(fp,3,1,0,0);wgpuRenderPassEncoderEnd(fp);wgpuRenderPassEncoderRelease(fp);
 }
 if(ctx.taaComp)wgpuBindGroupRelease(ctx.taaComp);
 g_proj=baseProj;nc=0;
 if(taaActive){g_prevVP=ctx.taaVP;g_taaCur=1-g_taaCur;g_taaFrame++;}

 g_cpuFrameMs=emscripten_get_now()-g_frameStart;g_frameIndex++;
 /* Timestamp readback must never submit with a mapped/pending buffer. */
 bool tsOk=g_tsSet&&wgpuBufferGetMapState(g_tsBuf[0])==WGPUBufferMapState_Unmapped
                    &&wgpuBufferGetMapState(g_tsBuf[1])==WGPUBufferMapState_Unmapped;
 if(tsOk){int ti0=g_tsIdx;
  wgpuCommandEncoderWriteTimestamp(enc,g_tsSet,1);wgpuCommandEncoderResolveQuerySet(enc,g_tsSet,0,2,g_tsResolve[ti0],0);
  wgpuCommandEncoderCopyBufferToBuffer(enc,g_tsResolve[ti0],0,g_tsBuf[ti0],0,16);}
 WGPUCommandBuffer cmd=wgpuCommandEncoderFinish(enc,NULL); wgpuQueueSubmit(g_q,1,&cmd);
 if(tsOk){int ti=g_tsIdx;g_tsMapped[ti]=0;wgpuBufferMapAsync(g_tsBuf[ti],WGPUMapMode_Read,0,16,tsMapped,(void*)(intptr_t)ti);
  g_tsPrev=ti;g_tsIdx=1-ti;}
 /* Emscripten's legacy swapchain presents from requestAnimationFrame. */
 wgpuCommandBufferRelease(cmd);
 wgpuRenderPassEncoderRelease(pass);
 wgpuCommandEncoderRelease(enc);wgpuTextureViewRelease(view);
}

static uint32_t texRowBytes(int w){return ((uint32_t)w*4u+255u)&~255u;}
static void texWriteLevel(WGPUTexture tex,int level,int w,int h,const uint8_t*src){
 uint32_t stride=texRowBytes(w);size_t bytes=(size_t)stride*(size_t)h;
 uint8_t*tmp=(uint8_t*)calloc(bytes,1);if(!tmp)return;
 for(int y=0;y<h;++y)memcpy(tmp+(size_t)y*stride,src+(size_t)y*(size_t)w*4,(size_t)w*4);
 WGPUImageCopyTexture d={0};d.texture=tex;d.mipLevel=(uint32_t)level;d.aspect=WGPUTextureAspect_All;
 WGPUTextureDataLayout l={0};l.bytesPerRow=stride;l.rowsPerImage=(uint32_t)h;
 WGPUExtent3D e={(uint32_t)w,(uint32_t)h,1};wgpuQueueWriteTexture(g_q,&d,tmp,bytes,&l,&e);free(tmp);
}
static uint8_t* texDownsample(const uint8_t*src,int sw,int sh,int*dw,int*dh){
 int w=sw>1?sw/2:1,h=sh>1?sh/2:1;*dw=w;*dh=h;uint8_t*dst=(uint8_t*)malloc((size_t)w*h*4);if(!dst)return NULL;
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){
  int x0=x*2,y0=y*2,x1=x0+1<sw?x0+1:x0,y1=y0+1<sh?y0+1:y0;
  const uint8_t*a=src+(size_t)(y0*sw+x0)*4;const uint8_t*b=src+(size_t)(y0*sw+x1)*4;
  const uint8_t*c=src+(size_t)(y1*sw+x0)*4;const uint8_t*d=src+(size_t)(y1*sw+x1)*4;uint8_t*o=dst+(size_t)(y*w+x)*4;
  for(int k=0;k<4;++k)o[k]=(uint8_t)(((int)a[k]+b[k]+c[k]+d[k])/4);
 }
 return dst;
}
static Texture2D texCreate(Image image,int srgb,int wantMips,const TextureOptions*opt){
 Texture2D o={0};if(!g_dev||!image.data||image.width<=0||image.height<=0||tn+1>=MT)return o;
 int levels=1;if(wantMips){int mw=image.width,mh=image.height;while(mw>1||mh>1){if(mw>1)mw/=2;if(mh>1)mh/=2;++levels;}}
 int id=++tn;WT*t=&tex[id];memset(t,0,sizeof*t);t->u=1;t->w=image.width;t->h=image.height;t->mips=levels;t->srgb=srgb;
 WGPUTextureDescriptor td={0};td.size.width=(uint32_t)image.width;td.size.height=(uint32_t)image.height;td.size.depthOrArrayLayers=1;
 td.mipLevelCount=(uint32_t)levels;td.sampleCount=1;td.dimension=WGPUTextureDimension_2D;
 td.format=srgb?WGPUTextureFormat_RGBA8UnormSrgb:WGPUTextureFormat_RGBA8Unorm;
 td.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;
 t->t=wgpuDeviceCreateTexture(g_dev,&td);t->v=wgpuTextureCreateView(t->t,NULL);
 if(opt){WGPUSamplerDescriptor sd={0};int mf=opt->minFilter;
  sd.minFilter=(mf==0||mf==2||mf==4)?WGPUFilterMode_Nearest:WGPUFilterMode_Linear;
  sd.mipmapFilter=(mf>=4)?WGPUMipmapFilterMode_Linear:WGPUMipmapFilterMode_Nearest;
  sd.magFilter=opt->magFilter==0?WGPUFilterMode_Nearest:WGPUFilterMode_Linear;
  sd.addressModeU=(opt->wrapS==1)?WGPUAddressMode_ClampToEdge:((opt->wrapS==2)?WGPUAddressMode_MirrorRepeat:WGPUAddressMode_Repeat);
  sd.addressModeV=(opt->wrapT==1)?WGPUAddressMode_ClampToEdge:((opt->wrapT==2)?WGPUAddressMode_MirrorRepeat:WGPUAddressMode_Repeat);
  sd.addressModeW=sd.addressModeV;sd.maxAnisotropy=1;t->samp=wgpuDeviceCreateSampler(g_dev,&sd);}
 const uint8_t*cur=(const uint8_t*)image.data;int cw=image.width,ch=image.height;
 for(int l=0;l<levels;++l){texWriteLevel(t->t,l,cw,ch,cur);if(l+1<levels){int nw=1,nh=1;uint8_t*next=texDownsample(cur,cw,ch,&nw,&nh);if(!next)break;if(cur!=(const uint8_t*)image.data)free((void*)cur);cur=next;cw=nw;ch=nh;}}
 if(cur!=(const uint8_t*)image.data)free((void*)cur);
 o.id=(unsigned)id;o.width=image.width;o.height=image.height;return o;
}
Texture2D LoadTextureFromImage(Image image){return texCreate(image,0,1,NULL);}
Texture2D LoadTextureFromImageSRGB(Image image){return texCreate(image,1,1,NULL);}
Texture2D LoadTextureFromImageEx(Image image,const TextureOptions *o){
 return texCreate(image,o?o->srgb:0,o?o->mipmaps:1,o);
}
void UnloadTexture(Texture2D t){if(!t.id||t.id>=MT)return;bgClear();if(tex[t.id].samp)wgpuSamplerRelease(tex[t.id].samp);if(tex[t.id].v)wgpuTextureViewRelease(tex[t.id].v);if(tex[t.id].t)wgpuTextureRelease(tex[t.id].t);memset(&tex[t.id],0,sizeof tex[t.id]);}
void UpdateTexture(Texture2D t,const void*pixels){if(!g_dev||!pixels||!t.id||t.id>=MT||!tex[t.id].t||tex[t.id].w<=0||tex[t.id].h<=0)return;texWriteLevel(tex[t.id].t,0,tex[t.id].w,tex[t.id].h,(const uint8_t*)pixels);}
Material LoadMaterialDefault(void){Material m={0};if(man+1>=MA)return m;int id=++man;WA*w=&mats[id];memset(w,0,sizeof*w);w->u=1;w->bc=(Color){255,255,255,255};w->met=0;w->rou=1.0f;
 m.id=(unsigned)id;m.baseColor=w->bc;m.metallic=w->met;m.roughness=w->rou;return m;}
void SetMaterialTexture(Material*m,int map,Texture2D t){if(!m||!m->id||m->id>=MA)return;WA*w=&mats[m->id];
 switch(map){case MATERIAL_MAP_ALBEDO:m->albedoMap=t;w->a=t.id;break;case MATERIAL_MAP_METALLICROUGHNESS:m->metallicRoughnessMap=t;w->m=t.id;break;
 case MATERIAL_MAP_NORMAL:m->normalMap=t;w->n=t.id;break;case MATERIAL_MAP_AO:m->aoMap=t;w->o=t.id;break;case MATERIAL_MAP_EMISSION:m->emissionMap=t;w->e=t.id;break;}}
void UnloadMaterial(Material m){if(m.id&&m.id<MA)memset(&mats[m.id],0,sizeof mats[m.id]);}
static void syncm(Material*m){if(!m||!m->id||m->id>=MA)return;WA*w=&mats[m->id];w->bc=m->baseColor;w->met=m->metallic;w->rou=m->roughness;if(m->albedoMap.id)w->a=m->albedoMap.id;if(m->metallicRoughnessMap.id)w->m=m->metallicRoughnessMap.id;}

static void b2_ensure_tex(unsigned tid){
  if(nb2c>0 && b2cmds[nb2c-1].texId==tid) return;
  if(nb2c>=256) return;
  b2cmds[nb2c].texId=tid; b2cmds[nb2c].start=n2; b2cmds[nb2c].count=0; nb2c++; b2cur_tex=tid;
}
static void b2_push(float px,float py,float u,float v,Color c){
  if(n2>=16384) return;
  if(nb2c==0) b2_ensure_tex(0);
  V2*vert=&b2[n2++];
  float W=(float)(gw>0?gw:1), H=(float)(gh>0?gh:1);
  vert->x=(px/W)*2.f-1.f; vert->y=1.f-(py/H)*2.f;
  vert->u=u; vert->v=v;
  vert->r=c.r/255.f;vert->g=c.g/255.f;vert->b=c.b/255.f;vert->a=c.a/255.f;
  b2cmds[nb2c-1].count++;
}
static void p2(float x,float y,Color c){b2_ensure_tex(0);b2_push(x,y,0,0,c);}
void vgraphics_webgpu_draw_rect(float x,float y,float w,float h,Color c){
  b2_ensure_tex(0);
  b2_push(x,y,0,0,c);b2_push(x+w,y,1,0,c);b2_push(x+w,y+h,1,1,c);
  b2_push(x,y,0,0,c);b2_push(x+w,y+h,1,1,c);b2_push(x,y+h,0,1,c);
}
void vgraphics_DrawVertexBuffer(const Vertex2D *vertices, int count, Texture2D texture){
  if(!vertices||count<=0) return;
  unsigned tid = texture.id;
  b2_ensure_tex(tid);
  for(int i=0;i<count;++i){
    const Vertex2D*src=&vertices[i];
    Color col={src->r,src->g,src->b,src->a};
    b2_push(src->x,src->y,src->u,src->v,col);
  }
}
static void p3(float x,float y,float z,float nx,float ny,float nz,float u,float v,Color c){if(n3>=65536)return;V3*p=&b3[n3++];p->x=x;p->y=y;p->z=z;p->nx=nx;p->ny=ny;p->nz=nz;p->u=u;p->v=v;p->r=c.r/255.f;p->g=c.g/255.f;p->b=c.b/255.f;p->a=c.a/255.f;}
static void pl(float x0,float y0,float z0,float x1,float y1,float z1,Color c){if(nl+2>8192)return;V3*a=&bl[nl++];a->x=x0;a->y=y0;a->z=z0;a->nx=0;a->ny=1;a->nz=0;a->u=0;a->v=0;a->r=c.r/255.f;a->g=c.g/255.f;a->b=c.b/255.f;a->a=c.a/255.f;V3*b=&bl[nl++];b->x=x1;b->y=y1;b->z=z1;b->nx=0;b->ny=1;b->nz=0;b->u=0;b->v=0;b->r=c.r/255.f;b->g=c.g/255.f;b->b=c.b/255.f;b->a=c.a/255.f;}
static void tri(float ax,float ay,float az,float bx,float by,float bz,float cx,float cy,float cz,float nx,float ny,float nz,Color c){p3(ax,ay,az,nx,ny,nz,0,0,c);p3(bx,by,bz,nx,ny,nz,1,0,c);p3(cx,cy,cz,nx,ny,nz,1,1,c);}
void DrawCube(Vector3 p,Vector3 s,Color c){float hx=s.x*.5f,hy=s.y*.5f,hz=s.z*.5f,x=p.x,y=p.y,z=p.z;
 tri(x-hx,y-hy,z+hz,x+hx,y-hy,z+hz,x+hx,y+hy,z+hz,0,0,1,c);tri(x-hx,y-hy,z+hz,x+hx,y+hy,z+hz,x-hx,y+hy,z+hz,0,0,1,c);
 tri(x+hx,y-hy,z-hz,x-hx,y-hy,z-hz,x-hx,y+hy,z-hz,0,0,-1,c);tri(x+hx,y-hy,z-hz,x-hx,y+hy,z-hz,x+hx,y+hy,z-hz,0,0,-1,c);
 tri(x+hx,y-hy,z+hz,x+hx,y-hy,z-hz,x+hx,y+hy,z-hz,1,0,0,c);tri(x+hx,y-hy,z+hz,x+hx,y+hy,z-hz,x+hx,y+hy,z+hz,1,0,0,c);
 tri(x-hx,y-hy,z-hz,x-hx,y-hy,z+hz,x-hx,y+hy,z+hz,-1,0,0,c);tri(x-hx,y-hy,z-hz,x-hx,y+hy,z+hz,x-hx,y+hy,z-hz,-1,0,0,c);
 tri(x-hx,y+hy,z+hz,x+hx,y+hy,z+hz,x+hx,y+hy,z-hz,0,1,0,c);tri(x-hx,y+hy,z+hz,x+hx,y+hy,z-hz,x-hx,y+hy,z-hz,0,1,0,c);
 tri(x-hx,y-hy,z-hz,x+hx,y-hy,z-hz,x+hx,y-hy,z+hz,0,-1,0,c);tri(x-hx,y-hy,z-hz,x+hx,y-hy,z+hz,x-hx,y-hy,z+hz,0,-1,0,c);}
void DrawSphere(Vector3 c,float r,Color col){const int S=12,L=16;for(int i=0;i<S;++i){float p0=(float)(i*M_PI/S),p1=(float)((i+1)*M_PI/S);
 for(int j=0;j<L;++j){float t0=(float)(j*2*M_PI/L),t1=(float)((j+1)*2*M_PI/L);
  float x00=sinf(p0)*cosf(t0),y00=cosf(p0),z00=sinf(p0)*sinf(t0),x01=sinf(p0)*cosf(t1),y01=cosf(p0),z01=sinf(p0)*sinf(t1);
  float x10=sinf(p1)*cosf(t0),y10=cosf(p1),z10=sinf(p1)*sinf(t0),x11=sinf(p1)*cosf(t1),y11=cosf(p1),z11=sinf(p1)*sinf(t1);
  float ax=c.x+r*x00,ay=c.y+r*y00,az=c.z+r*z00,bx=c.x+r*x01,by=c.y+r*y01,bz=c.z+r*z01,cx=c.x+r*x10,cy=c.y+r*y10,cz=c.z+r*z10,dx=c.x+r*x11,dy=c.y+r*y11,dz=c.z+r*z11;
  if(i>0){p3(ax,ay,az,x00,y00,z00,0,0,col);p3(cx,cy,cz,x10,y10,z10,0,1,col);p3(bx,by,bz,x01,y01,z01,1,0,col);}
  if(i<S-1){p3(bx,by,bz,x01,y01,z01,1,0,col);p3(cx,cy,cz,x10,y10,z10,0,1,col);p3(dx,dy,dz,x11,y11,z11,1,1,col);}}}}
void DrawGrid(int s,float sp){Color c={90,95,110,255};float h=s*sp*.5f;for(int i=0;i<=s;++i){float t=-h+i*sp;pl(t,0,-h,t,0,h,c);pl(-h,0,t,h,0,t,c);}}
void DrawLine3D(Vector3 a,Vector3 b,Color c){pl(a.x,a.y,a.z,b.x,b.y,b.z,c);}
void DrawMesh(Mesh mesh,Texture2D texture,Color baseColor,Matrix model){(void)texture;if(!mesh.id||nc>=MAX_DRAWS)return;DC*c=&cmds[nc++];c->mid=(int)mesh.id;c->aid=0;c->model=model;c->tint=baseColor.a?baseColor:(Color){255,255,255,255};}
void DrawModel(Model model,Vector3 position,Vector3 rotation,Vector3 scale,Color tint){
 if(!model.id||model.id>=MO||!models[model.id].u)return; WO*m=&models[model.id];
 Matrix T=MatrixTranslate(position.x,position.y,position.z),R=MatrixRotateXYZ(rotation),S=MatrixScale(scale.x,scale.y,scale.z);
 Matrix top=MatrixMultiply(T,MatrixMultiply(R,S));
 for(int i=0;i<m->c&&nc<MAX_DRAWS;++i){DC*c=&cmds[nc++];c->mid=m->mid[i];c->aid=m->aid[i];c->model=MatrixMultiply(top,m->xf[i]);c->tint=tint.a?tint:(Color){255,255,255,255};}
}
Mesh vgraphics_RegisterMesh(const Vertex3D*verts,int vc,const uint32_t*indices,int ic){
 Mesh o={0};if(!verts||vc<=0||mn+1>=MM)return o;int id=++mn;WM*m=&meshes[id];memset(m,0,sizeof*m);m->u=1;m->vc=vc;
 m->v=(V3*)malloc(sizeof(V3)*(size_t)vc);for(int i=0;i<vc;++i){m->v[i].x=verts[i].x;m->v[i].y=verts[i].y;m->v[i].z=verts[i].z;m->v[i].nx=verts[i].nx;m->v[i].ny=verts[i].ny;m->v[i].nz=verts[i].nz;m->v[i].u=verts[i].u;m->v[i].v=verts[i].v;m->v[i].tx=verts[i].tx;m->v[i].ty=verts[i].ty;m->v[i].tz=verts[i].tz;m->v[i].tw=verts[i].tw;m->v[i].r=m->v[i].g=m->v[i].b=m->v[i].a=1;}
 if(indices&&ic>0){m->ic=ic;m->i=(uint32_t*)malloc(sizeof(uint32_t)*(size_t)ic);memcpy(m->i,indices,sizeof(uint32_t)*(size_t)ic);}
 else{m->ic=vc;m->i=(uint32_t*)malloc(sizeof(uint32_t)*(size_t)vc);for(int i=0;i<vc;++i)m->i[i]=(uint32_t)i;}
 o.id=(unsigned)id;o.vertexCount=vc;o.indexCount=m->ic;return o;}
Model vgraphics_CreateModelFromMeshes(const Mesh*meshes,const int*mm,int mc,const Material*mats,int mac){return vgraphics_CreateModelFromMeshesEx(meshes,mm,NULL,mc,mats,mac);}
Model vgraphics_CreateModelFromMeshesEx(const Mesh*meshes,const int*mm,const Matrix*xf,int mc,const Material*matsIn,int mac){
 Model o={0};if(!meshes||mc<=0||mon+1>=MO)return o;int id=++mon;WO*m=&models[id];memset(m,0,sizeof*m);m->u=1;m->c=mc>32?32:mc;
 for(int i=0;i<m->c;++i){m->mid[i]=(int)meshes[i].id;m->xf[i]=xf?xf[i]:MatrixIdentity();int mi=mm?mm[i]:0;
  if(matsIn&&mi>=0&&mi<mac){Material mat=matsIn[mi];syncm(&mat);m->aid[i]=(int)mat.id;}else m->aid[i]=0;}
 o.id=(unsigned)id;o.meshCount=m->c;o.materialCount=mac;return o;}
Mesh GenMeshCube(float w,float h,float d){float hx=w*.5f,hy=h*.5f,hz=d*.5f;Vertex3D v[24];uint32_t ix[36];int vi=0,ii=0;
#define F(nx,ny,nz,x0,y0,z0,x1,y1,z1,x2,y2,z2,x3,y3,z3) do{int b=vi;v[vi++]=(Vertex3D){x0,y0,z0,nx,ny,nz,0,0};v[vi++]=(Vertex3D){x1,y1,z1,nx,ny,nz,1,0};v[vi++]=(Vertex3D){x2,y2,z2,nx,ny,nz,1,1};v[vi++]=(Vertex3D){x3,y3,z3,nx,ny,nz,0,1};ix[ii++]=b;ix[ii++]=b+1;ix[ii++]=b+2;ix[ii++]=b;ix[ii++]=b+2;ix[ii++]=b+3;}while(0)
 F(0,0,1,-hx,-hy,hz,hx,-hy,hz,hx,hy,hz,-hx,hy,hz);F(0,0,-1,hx,-hy,-hz,-hx,-hy,-hz,-hx,hy,-hz,hx,hy,-hz);
 F(1,0,0,hx,-hy,hz,hx,-hy,-hz,hx,hy,-hz,hx,hy,hz);F(-1,0,0,-hx,-hy,-hz,-hx,-hy,hz,-hx,hy,hz,-hx,hy,-hz);
 F(0,1,0,-hx,hy,hz,hx,hy,hz,hx,hy,-hz,-hx,hy,-hz);F(0,-1,0,-hx,-hy,-hz,hx,-hy,-hz,hx,-hy,hz,-hx,-hy,hz);
#undef F
 return vgraphics_RegisterMesh(v,24,ix,36);}
Mesh GenMeshSphere(float r,int rings,int slices){if(rings<3)rings=8;if(slices<3)slices=12;int vc=(rings+1)*(slices+1),ic=rings*slices*6;
 Vertex3D*v=(Vertex3D*)calloc((size_t)vc,sizeof(Vertex3D));uint32_t*ix=(uint32_t*)malloc(sizeof(uint32_t)*(size_t)ic);int vi=0,ii=0;
 for(int i=0;i<=rings;++i){float phi=(float)(i*M_PI/rings);for(int j=0;j<=slices;++j){float th=(float)(j*2*M_PI/slices);float x=sinf(phi)*cosf(th),y=cosf(phi),z=sinf(phi)*sinf(th);v[vi++]=(Vertex3D){r*x,r*y,r*z,x,y,z,(float)j/slices,(float)i/rings};}}
 for(int i=0;i<rings;++i)for(int j=0;j<slices;++j){uint32_t a=(uint32_t)(i*(slices+1)+j),b=a+(uint32_t)(slices+1);ix[ii++]=a;ix[ii++]=b;ix[ii++]=a+1;ix[ii++]=a+1;ix[ii++]=b;ix[ii++]=b+1;}
 Mesh m=vgraphics_RegisterMesh(v,vc,ix,ic);free(v);free(ix);return m;}
void UnloadMesh(Mesh mesh){if(mesh.id&&mesh.id<MM){free(meshes[mesh.id].v);free(meshes[mesh.id].i);memset(&meshes[mesh.id],0,sizeof meshes[mesh.id]);}}
Model LoadModel(const char*path){extern Model LoadModelGLTFTiny(const char*);if(path){Model m=LoadModelGLTFTiny(path);if(m.id)return m;}Model z={0};return z;}
Model LoadModelFromMesh(Mesh mesh){Material mat=LoadMaterialDefault();int mi=0;return vgraphics_CreateModelFromMeshes(&mesh,&mi,1,&mat,1);}
void UnloadModel(Model model){if(model.id&&model.id<MO)memset(&models[model.id],0,sizeof models[model.id]);}
/* ===================== Skinning (public API) ====================== */
static Matrix skDrawTmp[VLCR_SKIN_BONES];
static Matrix skDrawTmp2[VLCR_SKIN_BONES];
static Matrix skPoseLocal[VLCR_SKIN_BONES];
static Quaternion skWorldQ[VLCR_SKIN_BONES];
static unsigned char skQDone[VLCR_SKIN_BONES];

static SkModel* skModelGet(SkinnedModel m){
 if(!skModels||m.id==0||m.id>=VLCR_SKIN_MODELS)return NULL;
 SkModel*e=&skModels[m.id];return e->u?e:NULL;
}
static SkAnim* skAnimGet(unsigned id){
 if(!skAnims||id==0||id>(unsigned)skAnimN)return NULL;
 SkAnim*a=&skAnims[id];return a->skel?a:NULL;
}
static int skAddAnimTable(void){
 for(int i=1;i<=skAnimN;++i)if(!skAnims[i].skel&&!skAnims[i].name[0])return i;
 if(skAnimN+1>=skAnimCap){
  int nc=skAnimCap?skAnimCap*2:16;if(nc>VLCR_SKIN_ANIMS)nc=VLCR_SKIN_ANIMS;
  if(skAnimN+1>=nc)return 0;
  SkAnim*p=(SkAnim*)realloc(skAnims,sizeof(SkAnim)*(size_t)nc);
  if(!p)return 0;skAnims=p;skAnimCap=nc;
 }
 return ++skAnimN;
}
static void skFreeAnim(SkAnim*a){
 if(!a)return;
 if(a->tracks){
  for(int b=0;b<a->trackCount;++b){free(a->tracks[b].pos);free(a->tracks[b].rot);free(a->tracks[b].scale);}
  free(a->tracks);
 }
 memset(a,0,sizeof(*a));
}
static void skFreeModelEntry(SkModel*e){
 if(!e)return;
 free(e->verts);free(e->idx);free(e->animIds);free(e->skel);
 memset(e,0,sizeof(*e));
}

SkinnedModel vgraphics_CreateSkinnedModel(
    const SkinnedVertex3D *vertices, int vertexCount,
    const uint32_t *indices, int indexCount,
    const Material *material,
    const Matrix *inverseBind,
    const int *parents,
    const Vector3 *restT, const Quaternion *restR, const Vector3 *restS,
    int boneCount, const char *const *boneNames)
{
 SkinnedModel out={0};
 if(!vertices||vertexCount<=0||!indices||indexCount<=0)return out;
 if(boneCount<=0||boneCount>VLCR_SKIN_BONES)return out;
 if(!skModels){
  skModels=(SkModel*)calloc(VLCR_SKIN_MODELS,sizeof(SkModel));
  if(!skModels)return out;
 }
 int id=0;for(int i=1;i<VLCR_SKIN_MODELS;++i)if(!skModels[i].u){id=i;break;}
 if(!id)return out;
 SkModel*e=&skModels[id];memset(e,0,sizeof(*e));
 e->skel=(SkSkel*)calloc(1,sizeof(SkSkel));
 e->verts=(SkinnedVertex3D*)malloc(sizeof(SkinnedVertex3D)*(size_t)vertexCount);
 e->idx=(uint32_t*)malloc(sizeof(uint32_t)*(size_t)indexCount);
 e->animIds=(int*)calloc(2,sizeof(int));e->animCap=2;
 if(!e->skel||!e->verts||!e->idx||!e->animIds){skFreeModelEntry(e);return out;}
 memcpy(e->verts,vertices,sizeof(SkinnedVertex3D)*(size_t)vertexCount);
 memcpy(e->idx,indices,sizeof(uint32_t)*(size_t)indexCount);
 e->vc=vertexCount;e->ic=indexCount;
 e->skel->boneCount=boneCount;
 for(int b=0;b<boneCount;++b){
  SkBone*bo=&e->skel->bones[b];
  if(boneNames&&boneNames[b])snprintf(bo->name,sizeof(bo->name),"%s",boneNames[b]);
  bo->parent=parents?parents[b]:-1;
  if(bo->parent>=boneCount)bo->parent=-1;
  bo->inv=inverseBind?inverseBind[b]:MatrixIdentity();
  bo->rt=restT?restT[b]:(Vector3){0,0,0};
  bo->rr=restR?restR[b]:(Quaternion){0,0,0,1};
  bo->rs=restS?restS[b]:(Vector3){1,1,1};
 }
 e->mat=material?*material:LoadMaterialDefault();
 syncm(&e->mat);
 e->bmin=(Vector3){1e30f,1e30f,1e30f};e->bmax=(Vector3){-1e30f,-1e30f,-1e30f};
 for(int i=0;i<vertexCount;++i){
  const SkinnedVertex3D*v=&vertices[i];
  if(v->x<e->bmin.x)e->bmin.x=v->x;if(v->x>e->bmax.x)e->bmax.x=v->x;
  if(v->y<e->bmin.y)e->bmin.y=v->y;if(v->y>e->bmax.y)e->bmax.y=v->y;
  if(v->z<e->bmin.z)e->bmin.z=v->z;if(v->z>e->bmax.z)e->bmax.z=v->z;
 }
 e->hasBounds=1;
 int aid=skAddAnimTable();
 if(!aid){skFreeModelEntry(e);return out;}
 SkAnim*a=&skAnims[aid];memset(a,0,sizeof(*a));
 snprintf(a->name,sizeof(a->name),"bind");
 a->duration=1.0f;a->skel=e->skel;
 a->tracks=(SkTrack*)calloc((size_t)boneCount,sizeof(SkTrack));
 if(!a->tracks){skFreeAnim(a);skFreeModelEntry(e);return out;}
 a->trackCount=boneCount;
 e->animIds[0]=aid;e->animCount=1;
 e->u=1;
 out.id=(unsigned)id;out.meshCount=1;out.materialCount=1;
 out.skeleton=id;out.animationCount=1;
 return out;
}

int vgraphics_AddSkinnedAnimation(SkinnedModel model, const char *name,
                                  float duration,
                                  const AnimTrackData *tracks, int trackCount)
{
 SkModel*e=skModelGet(model);
 if(!e||!tracks||trackCount<=0)return -1;
 int bc=e->skel->boneCount;if(trackCount>bc)trackCount=bc;
 int aid=skAddAnimTable();
 if(!aid)return -1;
 SkAnim*a=&skAnims[aid];memset(a,0,sizeof(*a));
 snprintf(a->name,sizeof(a->name),"%s",name&&name[0]?name:"clip");
 a->duration=duration>0.0f?duration:1.0f;
 a->skel=e->skel;
 a->tracks=(SkTrack*)calloc((size_t)bc,sizeof(SkTrack));
 if(!a->tracks){memset(a,0,sizeof(*a));return -1;}
 a->trackCount=bc;
 for(int b=0;b<trackCount;++b){
  const AnimTrackData*t=&tracks[b];SkTrack*tr=&a->tracks[b];
  if(t->posCount>0&&t->pos){
   tr->pos=(SkVecKey*)malloc(sizeof(SkVecKey)*(size_t)t->posCount);
   if(tr->pos){for(int i=0;i<t->posCount;++i){tr->pos[i].t=t->pos[i].t;tr->pos[i].v=t->pos[i].v;}tr->posCount=t->posCount;}
  }
  if(t->rotCount>0&&t->rot){
   tr->rot=(SkQuatKey*)malloc(sizeof(SkQuatKey)*(size_t)t->rotCount);
   if(tr->rot){for(int i=0;i<t->rotCount;++i){tr->rot[i].t=t->rot[i].t;tr->rot[i].q=t->rot[i].q;}tr->rotCount=t->rotCount;}
  }
  if(t->scaleCount>0&&t->scale){
   tr->scale=(SkVecKey*)malloc(sizeof(SkVecKey)*(size_t)t->scaleCount);
   if(tr->scale){for(int i=0;i<t->scaleCount;++i){tr->scale[i].t=t->scale[i].t;tr->scale[i].v=t->scale[i].v;}tr->scaleCount=t->scaleCount;}
  }
 }
 if(e->animCount+1>e->animCap){
  int nc=e->animCap*2;if(nc<4)nc=4;
  int*g=(int*)realloc(e->animIds,sizeof(int)*(size_t)nc);
  if(!g){skFreeAnim(a);return -1;}e->animIds=g;e->animCap=nc;
 }
 e->animIds[e->animCount++]=aid;
 return aid;
}

const char *GetSkinnedModelAnimationName(SkinnedModel model, int animIndex){
 SkModel*e=skModelGet(model);
 if(!e||animIndex<0||animIndex>=e->animCount)return "";
 SkAnim*a=skAnimGet((unsigned)e->animIds[animIndex]);
 return a?a->name:"";
}
int GetSkinnedModelAnimationCount(SkinnedModel model){
 SkModel*e=skModelGet(model);return e?e->animCount:0;
}
int GetSkinnedModelBoneCount(SkinnedModel model){
 SkModel*e=skModelGet(model);return e?e->skel->boneCount:0;
}
const char *GetSkinnedModelBoneName(SkinnedModel model, int boneIndex){
 SkModel*e=skModelGet(model);
 if(!e||boneIndex<0||boneIndex>=e->skel->boneCount)return "";
 return e->skel->bones[boneIndex].name;
}
bool GetSkinnedModelBounds(SkinnedModel model, Vector3 *outMin, Vector3 *outMax){
 SkModel*e=skModelGet(model);
 if(!e||!e->hasBounds)return false;
 if(outMin)*outMin=e->bmin;if(outMax)*outMax=e->bmax;
 return true;
}
float GetSkinnedModelAnimationDuration(SkinnedModel model, int animIndex){
 SkModel*e=skModelGet(model);
 if(!e||animIndex<0||animIndex>=e->animCount)return 0;
 SkAnim*a=skAnimGet((unsigned)e->animIds[animIndex]);
 return a?a->duration:0;
}
Animation GetSkinnedModelAnimation(SkinnedModel model, int animIndex){
 Animation anim={0};
 SkModel*e=skModelGet(model);
 if(!e||animIndex<0||animIndex>=e->animCount)return anim;
 SkAnim*a=skAnimGet((unsigned)e->animIds[animIndex]);
 if(!a)return anim;
 anim.id=(unsigned)e->animIds[animIndex];
 anim.duration=a->duration;
 anim.boneCount=a->skel->boneCount;
 return anim;
}
bool GetAnimationRootMotion(Animation animation, float fromTime, float toTime,
                            Vector3 *outDelta){
 if(outDelta)*outDelta=(Vector3){0};
 SkAnim*a=skAnimGet(animation.id);
 if(!a)return false;
 SkSkel*s=a->skel;
 for(int b=0;b<s->boneCount;++b){
  if(s->bones[b].parent>=0)continue;
  SkTrack*tr=&a->tracks[b];
  if(tr->posCount<=0||!tr->pos)continue;
  Vector3 p0,p1;
  skSampleVec(tr->pos,tr->posCount,fromTime,s->bones[b].rt,&p0);
  skSampleVec(tr->pos,tr->posCount,toTime,s->bones[b].rt,&p1);
  if(outDelta)*outDelta=Vector3Subtract(p1,p0);
  return true;
 }
 return false;
}
void SetSkinnedModelMaterial(SkinnedModel model, int idx, Material mat){
 SkModel*e=skModelGet(model);
 if(!e||idx!=0)return;
 e->mat=mat;syncm(&e->mat);
}
bool GetSkinnedModelRestPose(SkinnedModel model, BonePose *out, int maxBones){
 SkModel*e=skModelGet(model);
 if(!e||!out)return false;
 SkSkel*s=e->skel;int n=s->boneCount<maxBones?s->boneCount:maxBones;
 for(int b=0;b<n;++b){out[b].position=s->bones[b].rt;out[b].rotation=s->bones[b].rr;out[b].scale=s->bones[b].rs;}
 return true;
}
bool GetAnimationPose(Animation animation, float time, BonePose *out, int maxBones){
 SkAnim*a=skAnimGet(animation.id);
 if(!a||!out)return false;
 SkSkel*s=a->skel;int n=s->boneCount<maxBones?s->boneCount:maxBones;
 for(int b=0;b<n;++b){
  SkTrack*tr=&a->tracks[b];
  skSampleVec(tr->pos,tr->posCount,time,s->bones[b].rt,&out[b].position);
  skSampleQuat(tr->rot,tr->rotCount,time,s->bones[b].rr,&out[b].rotation);
  skSampleVec(tr->scale,tr->scaleCount,time,s->bones[b].rs,&out[b].scale);
 }
 return true;
}
void DrawSkinnedModel(SkinnedModel model, Animation anim, float time,
                      Vector3 position, Vector3 rotation, Vector3 scale, Color tint){
 SkModel*e=skModelGet(model);SkAnim*a=skAnimGet(anim.id);
 if(!e||!a||a->skel!=e->skel)return;
 int n=e->skel->boneCount;
 if(strcmp(a->name,"bind")==0){
  for(int b=0;b<n&&b<VLCR_SKIN_BONES;++b)skDrawTmp[b]=MatrixIdentity();
 }else skEvalAnim(a,time,skDrawTmp);
 skQueueDraw((int)model.id,skDrawTmp,n,skModelMatrix(position,rotation,scale),tint);
}
void DrawSkinnedModelBlend(SkinnedModel model,
                           Animation from, float fromTime,
                           Animation to, float toTime, float blend,
                           Vector3 position, Vector3 rotation, Vector3 scale, Color tint){
 SkModel*e=skModelGet(model);SkAnim*a=skAnimGet(from.id);SkAnim*b=skAnimGet(to.id);
 if(!e||!a||!b||a->skel!=e->skel||b->skel!=e->skel)return;
 float w=blend<0?0:(blend>1?1:blend);
 skEvalAnim(a,fromTime,skDrawTmp);
 skEvalAnim(b,toTime,skDrawTmp2);
 int n=e->skel->boneCount;
 for(int i=0;i<n&&i<VLCR_SKIN_BONES;++i){
  const float*pa=(const float*)&skDrawTmp[i];const float*pb=(const float*)&skDrawTmp2[i];
  float*po=(float*)&skDrawTmp[i];
  for(int k=0;k<16;++k)po[k]=pa[k]*(1.0f-w)+pb[k]*w;
 }
 skQueueDraw((int)model.id,skDrawTmp,n,skModelMatrix(position,rotation,scale),tint);
}
void DrawSkinnedModelPose(SkinnedModel model, const BonePose *pose, int boneCount,
                          Vector3 position, Vector3 rotation, Vector3 scale, Color tint){
 SkModel*e=skModelGet(model);
 if(!e||!pose)return;
 int n=e->skel->boneCount;if(boneCount<n)n=boneCount;
 skLocalFromPose(e->skel,pose,n,skPoseLocal);
 skSkinFromLocal(e->skel,skPoseLocal,n,skDrawTmp);
 skQueueDraw((int)model.id,skDrawTmp,n,skModelMatrix(position,rotation,scale),tint);
}
SkinnedModel LoadSkinnedModel(const char *path){
 extern SkinnedModel vgltf_LoadSkinnedModel(const char *);
 if(!g_dev||!path||!path[0])return (SkinnedModel){0};
 return vgltf_LoadSkinnedModel(path);
}
void UnloadSkinnedModel(SkinnedModel model){
 SkModel*e=skModelGet(model);
 if(!e)return;
 for(int i=1;i<=skAnimN;++i)if(skAnims[i].skel==e->skel)skFreeAnim(&skAnims[i]);
 skFreeModelEntry(e);
}
void vgraphics_UploadBoneMatrices(const Matrix *matrices, int count){
 (void)matrices;(void)count;
}

/* ---- two-bone IK -------------------------------------------------- */
static Quaternion ikQInv(Quaternion q){return (Quaternion){-q.x,-q.y,-q.z,q.w};}
static Quaternion ikQFromTo(Vector3 from,Vector3 to){
 from=Vector3Normalize(from);to=Vector3Normalize(to);
 float d=Vector3Dot(from,to);
 if(d>0.99999f)return QuaternionIdentity();
 if(d<-0.99999f){
  Vector3 axis=fabsf(from.y)>0.9f?(Vector3){1,0,0}:(Vector3){0,1,0};
  axis=Vector3Normalize(Vector3Cross(from,axis));
  return QuaternionFromAxisAngle(axis,3.14159265f);
 }
 Vector3 c=Vector3Cross(from,to);
 return QuaternionNormalize((Quaternion){c.x,c.y,c.z,1.0f+d});
}
static Quaternion ikWorldRotation(SkSkel*s,const BonePose*pose,int b,Quaternion*cache,unsigned char*done){
 if(done[b])return cache[b];
 int parent=s->bones[b].parent;
 Quaternion pq=QuaternionIdentity();
 if(parent>=0&&parent<s->boneCount&&parent!=b)pq=ikWorldRotation(s,pose,parent,cache,done);
 cache[b]=QuaternionNormalize(QuaternionMultiply(pq,pose[b].rotation));
 done[b]=1;return cache[b];
}
bool ApplyTwoBoneIKToPose(SkinnedModel model, BonePose *pose, int boneCount,
                          int upperBone, int midBone, int endBone,
                          Vector3 target, Vector3 pole){
 SkModel*e=skModelGet(model);
 if(!e||!pose)return false;
 SkSkel*s=e->skel;
 int n=s->boneCount;if(boneCount<n)n=boneCount;
 if(upperBone<0||midBone<0||endBone<0||upperBone>=n||midBone>=n||endBone>=n)return false;
 skLocalFromPose(s,pose,n,skPoseLocal);
 memset(skDoneTmp,0,sizeof(unsigned char)*(size_t)VLCR_SKIN_BONES);
 for(int b=0;b<n;++b)skBoneGlobal(s,b,skPoseLocal,skGlobalTmp,skDoneTmp);
 Vector3 pUpper={skGlobalTmp[upperBone].m12,skGlobalTmp[upperBone].m13,skGlobalTmp[upperBone].m14};
 Vector3 pMid={skGlobalTmp[midBone].m12,skGlobalTmp[midBone].m13,skGlobalTmp[midBone].m14};
 Vector3 pEnd={skGlobalTmp[endBone].m12,skGlobalTmp[endBone].m13,skGlobalTmp[endBone].m14};
 Vector3 newMid,newEnd;
 if(!Vector3SolveTwoBoneIK(pUpper,pMid,pEnd,target,pole,&newMid,&newEnd))return false;
 memset(skQDone,0,sizeof(unsigned char)*(size_t)VLCR_SKIN_BONES);
 for(int b=0;b<n;++b)ikWorldRotation(s,pose,b,skWorldQ,skQDone);
 int parentUpper=s->bones[upperBone].parent;
 Quaternion pq=(parentUpper>=0&&parentUpper<n)?skWorldQ[parentUpper]:QuaternionIdentity();
 Quaternion dU=ikQFromTo(Vector3Subtract(pMid,pUpper),Vector3Subtract(newMid,pUpper));
 Quaternion upW=QuaternionNormalize(QuaternionMultiply(dU,skWorldQ[upperBone]));
 pose[upperBone].rotation=QuaternionNormalize(QuaternionMultiply(ikQInv(pq),upW));
 Quaternion dL=ikQFromTo(Vector3Subtract(pEnd,pMid),Vector3Subtract(newEnd,newMid));
 Quaternion midW=QuaternionNormalize(QuaternionMultiply(dL,skWorldQ[midBone]));
 pose[midBone].rotation=QuaternionNormalize(QuaternionMultiply(ikQInv(upW),midW));
 return true;
}
Shader LoadShader(const char*a,const char*b){(void)a;(void)b;Shader s={0};return s;}
void UnloadShader(Shader s){(void)s;}
/* Feature surface kept link-compatible with the Vulkan backend. These are
   no-ops on WebGPU until the corresponding passes are ported. */
void SetTAA(int e){g_taaEnabled=e?1:0;if(e)g_taaFrame=0;}
void SetFXAA(int e){g_fxaa=e?1:0;}
void SetShadowQuality(float bias,float strength,float softRadius){(void)bias;(void)strength;(void)softRadius;}
void vgraphics_SetShadows(bool e,int mapSize,float bias,float strength){(void)mapSize;g_shadowEnabled=e?1:0;if(!e)g_shadowValid=0;if(bias>0)g_shadowBias=bias;g_shadowStrength=strength<0?0:(strength>1?1:strength);}
void vgraphics_SetShadowSoftness(float r){g_shadowSoftness=r<1?1:(r>3?3:r);}
void vgraphics_ComputeDirectionalShadowMatrix(float radius,float nearZ,float farZ){(void)radius;(void)nearZ;(void)farZ;}
void vgraphics_ComputeDirectionalShadowCascades(void){}
void vgraphics_ComputeSpotShadowMatrix(float nearZ,float farZ){(void)nearZ;(void)farZ;}
void vgraphics_UpdateShadowMatrix(Matrix m){(void)m;}
void vgraphics_SetSpotShadows(bool e,int mapSize,float bias,float strength){(void)e;(void)mapSize;(void)bias;(void)strength;}
void vgraphics_SetPointShadows(bool e,int mapSize,float bias,float strength){(void)e;(void)mapSize;(void)bias;(void)strength;}
void vgraphics_GetFrameStats(FrameStats*out){
 if(!out)return;memset(out,0,sizeof(*out));
 out->frameIndex=g_frameIndex;out->width=gw;out->height=gh;
 out->drawCalls=g_statDraws;out->shadowDraws=g_statShadowDraws;out->triangles=g_statTris;
 out->textureCount=tn;out->meshCount=mn;out->cpuFrameMs=(float)g_cpuFrameMs;
 out->gpuFrameMs=g_tsSampled?g_gpuFrameMs:-1.0f;
 uint64_t px=(uint64_t)(gw>0?gw:1)*(uint64_t)(gh>0?gh:1);
 out->estimatedGpuBytes=(uint64_t)MAX_DRAWS*UBO_STRIDE*2+(uint64_t)MAX_FRAME_VERTS*sizeof(V3)*2+
  px*4+px*4+px*4+px*8+px+px;
}
void vgraphics_ResetFrameStats(void){g_statDraws=0;g_statShadowDraws=0;g_statTris=0;g_cpuFrameMs=0;}
bool vgraphics_CaptureScreenshot(const char*path){
 if(!g_dev||!g_sceneTex||!path||!*path||gw<=0||gh<=0)return false;
 int w=gw,h=gh;uint32_t row=((uint32_t)w*4u+255u)&~255u;size_t bytes=(size_t)row*(size_t)h;
 WGPUBufferDescriptor bd={0};bd.usage=WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst;bd.size=bytes;
 WGPUBuffer buf=wgpuDeviceCreateBuffer(g_dev,&bd);if(!buf)return false;
 WGPUCommandEncoder enc=wgpuDeviceCreateCommandEncoder(g_dev,NULL);
 WGPUImageCopyTexture src={0};src.texture=g_finalTex?g_finalTex:g_sceneTex;src.aspect=WGPUTextureAspect_All;
 WGPUImageCopyBuffer dst={0};dst.buffer=buf;dst.layout.bytesPerRow=row;dst.layout.rowsPerImage=(uint32_t)h;
 WGPUExtent3D ext={(uint32_t)w,(uint32_t)h,1};
 wgpuCommandEncoderCopyTextureToBuffer(enc,&src,&dst,&ext);
 WGPUCommandBuffer cmd=wgpuCommandEncoderFinish(enc,NULL);wgpuQueueSubmit(g_q,1,&cmd);
 wgpuCommandBufferRelease(cmd);wgpuCommandEncoderRelease(enc);
 g_shotMapDone=0;g_shotMapStatus=WGPUBufferMapAsyncStatus_Unknown;
 wgpuBufferMapAsync(buf,WGPUMapMode_Read,0,bytes,shotMapped,NULL);
 int spins=0;while(!g_shotMapDone&&spins++<5000)emscripten_sleep(1);
 bool ok=g_shotMapDone&&g_shotMapStatus==WGPUBufferMapAsyncStatus_Success;
 if(ok){
  const uint8_t*data=(const uint8_t*)wgpuBufferGetConstMappedRange(buf,0,bytes);
  uint8_t*rgba=(uint8_t*)malloc((size_t)w*(size_t)h*4);
  if(data&&rgba){
   bool bgra=(g_fmt==WGPUTextureFormat_BGRA8Unorm||g_fmt==WGPUTextureFormat_BGRA8UnormSrgb);
   for(int y=0;y<h;++y){const uint8_t*s=data+(size_t)y*row;uint8_t*d=rgba+(size_t)y*w*4;
    for(int x=0;x<w;++x){if(bgra){d[x*4+0]=s[x*4+2];d[x*4+1]=s[x*4+1];d[x*4+2]=s[x*4+0];}
     else{d[x*4+0]=s[x*4+0];d[x*4+1]=s[x*4+1];d[x*4+2]=s[x*4+2];}d[x*4+3]=s[x*4+3];}}
   ok=stbi_write_png(path,w,h,4,rgba,w*4)!=0;
   if(ok){
#if defined(__EMSCRIPTEN__)
    /* Opt-in: headless validation must not start a browser download. */
    EM_ASM({try{if(window.vlcrDownloadScreenshots){var p=UTF8ToString($0);var d=FS.readFile(p,{encoding:'binary'});var b=new Blob([d],{type:'image/png'});var a=document.createElement('a');a.href=URL.createObjectURL(b);a.download=p.split('/').pop();a.click();}}catch(e){console.error(e);}},path);
#endif
   }
  }else ok=false;
  free(rgba);
 }
 if(g_shotMapDone)wgpuBufferUnmap(buf);
 wgpuBufferRelease(buf);return ok;
}
bool vgraphics_IsIBLLoaded(void){return g_iblLoaded;}
void vgraphics_UnloadIBL(void){bgClear();releaseIbl();g_iblIntensity=0.0f;}
bool vgraphics_LoadHDRCubemap(const char*path,int cubeSize){(void)path;(void)cubeSize;return false;}
void vgraphics_EnableStudioLighting(void){vgraphics_SetDirectionalLight(true,(Vector3){-0.35f,-0.85f,-0.40f},(Color){255,248,240,255},2.8f);vgraphics_SetAmbientLight(true,(Color){90,100,125,255},0.45f);}
void vgraphics_SetPointLight(bool e,Vector3 p,Color c,float i,float r){
 PointL*l=&g_points[0];l->on=e&&i>0.0f&&r>0.1f?1:0;l->pos=p;l->color=c;l->intensity=i;l->range=r>0.1f?r:10.0f;
 static const Vector3 dirs[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
 static const Vector3 ups[6]={{0,1,0},{0,1,0},{0,0,1},{0,0,1},{0,1,0},{0,1,0}};
 Matrix proj=MatrixPerspective(1.5707963f,1.0f,0.1f,l->range);
 for(int f=0;f<6;++f){Vector3 t={p.x+dirs[f].x,p.y+dirs[f].y,p.z+dirs[f].z};
  Matrix view=MatrixLookAt(p,t,ups[f]);g_pointVP[f]=MatrixMultiply(proj,view);}
}
void vgraphics_SetSpotLight(bool e,Vector3 p,Vector3 d,Color c,float i,float r,float a,float b){
 (void)a;g_spotOn=e?1:0;g_spotPos=p;g_spotColor=c;g_spotIntensity=i;g_spotRange=r>0.1f?r:10.0f;
 float l=sqrtf(d.x*d.x+d.y*d.y+d.z*d.z);if(l<1e-5f)d=(Vector3){0,-1,0};else{d.x/=l;d.y/=l;d.z/=l;}g_spotDir=d;
 float outer=b>0.02f?b:0.6f;g_spotCos=cosf(outer);
 Vector3 target={p.x+d.x,p.y+d.y,p.z+d.z};
 Matrix view=MatrixLookAt(p,target,(Vector3){0,1,0});
 Matrix proj=MatrixPerspective(outer*2.0f,1.0f,0.1f,g_spotRange);
 g_spotVP=MatrixMultiply(proj,view);
}
void vgraphics_SetAreaLight(bool e,Vector3 p,Vector3 n,float w,float h,Color c,float i){(void)e;(void)p;(void)n;(void)w;(void)h;(void)c;(void)i;}
VLightData VLightDataDirectional(Vector3 direction,Color color,float intensity){
 VLightData l;memset(&l,0,sizeof l);l.direction[0]=direction.x;l.direction[1]=direction.y;l.direction[2]=direction.z;
 l.color[0]=color.r/255.f*intensity;l.color[1]=color.g/255.f*intensity;l.color[2]=color.b/255.f*intensity;l.color[3]=1;
 l.params[0]=0;l.params[1]=1;l.params[2]=-1;return l;
}
VLightData VLightDataPoint(Vector3 position,Color color,float intensity,float range){
 VLightData l;memset(&l,0,sizeof l);l.position[0]=position.x;l.position[1]=position.y;l.position[2]=position.z;
 l.position[3]=range>0?range:10;l.color[0]=color.r/255.f*intensity;l.color[1]=color.g/255.f*intensity;
 l.color[2]=color.b/255.f*intensity;l.color[3]=1;l.params[0]=1;l.params[1]=1;l.params[2]=-1;return l;
}
VLightData VLightDataSpot(Vector3 position,Vector3 direction,Color color,float intensity,float range,float innerCone,float outerCone){
 VLightData l;memset(&l,0,sizeof l);l.position[0]=position.x;l.position[1]=position.y;l.position[2]=position.z;
 l.position[3]=range>0?range:12;float dl=sqrtf(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
 if(dl>1e-6f){l.direction[0]=direction.x/dl;l.direction[1]=direction.y/dl;l.direction[2]=direction.z/dl;}
 else{l.direction[0]=0;l.direction[1]=-1;l.direction[2]=0;}
 l.direction[3]=cosf(innerCone*0.5f);l.color[0]=color.r/255.f*intensity;l.color[1]=color.g/255.f*intensity;
 l.color[2]=color.b/255.f*intensity;l.color[3]=cosf(outerCone*0.5f);l.params[0]=2;l.params[1]=1;l.params[2]=-1;return l;
}
VLightData VLightDataArea(Vector3 position,Vector3 normal,float width,float height,Color color,float intensity){
 VLightData l=VLightDataPoint(position,color,intensity,10);l.direction[0]=normal.x;l.direction[1]=normal.y;l.direction[2]=normal.z;
 l.params[0]=3;l.params[3]=width>0.01f?width:0.01f;l.extra[0]=height>0.01f?height:0.01f;return l;
}
void vgraphics_SetLightArray(const VLightData*lights,int count){
 vgraphics_ClearLights();for(int i=0;i<count;++i)vgraphics_AddLight(&lights[i]);
}
int vgraphics_AddLight(const VLightData*light){
 if(!light)return -1;
 if((int)light->params[0]==1){
  int pi=-1;for(int k=1;k<4;++k)if(!g_points[k].on){pi=k;break;}
  if(pi<0)return -1;PointL*l=&g_points[pi];l->on=1;
  l->pos=(Vector3){light->position[0],light->position[1],light->position[2]};
  l->range=light->position[3]>0.1f?light->position[3]:10.0f;
  float cr=light->color[0],cg=light->color[1],cb=light->color[2];
  l->color=(Color){(unsigned char)(cr>1?255:(cr<0?0:cr*255)),
                   (unsigned char)(cg>1?255:(cg<0?0:cg*255)),
                   (unsigned char)(cb>1?255:(cb<0?0:cb*255)),255};
  l->intensity=1.0f;
  static const Vector3 dirs[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  static const Vector3 ups[6]={{0,1,0},{0,1,0},{0,0,1},{0,0,1},{0,1,0},{0,1,0}};
  Matrix proj=MatrixPerspective(1.5707963f,1.0f,0.1f,l->range);
  Matrix*out=pi==1?g_point2VP:(pi==2?g_point3VP:(pi==3?g_point4VP:NULL));
  if(out)for(int f=0;f<6;++f){Vector3 t={l->pos.x+dirs[f].x,l->pos.y+dirs[f].y,l->pos.z+dirs[f].z};
   Matrix view=MatrixLookAt(l->pos,t,ups[f]);out[f]=MatrixMultiply(proj,view);}
  return pi;
 }
 if((int)light->params[0]!=2)return -1;
 int i=-1;for(int k=0;k<3;++k)if(!g_extraSpots[k].on){i=k;break;}
 if(i<0)return -1;ExtraSpot*s=&g_extraSpots[i];
 s->pos=(Vector3){light->position[0],light->position[1],light->position[2]};
 s->dir=(Vector3){light->direction[0],light->direction[1],light->direction[2]};
 float dl=sqrtf(s->dir.x*s->dir.x+s->dir.y*s->dir.y+s->dir.z*s->dir.z);
 if(dl>1e-5f){s->dir.x/=dl;s->dir.y/=dl;s->dir.z/=dl;}else s->dir=(Vector3){0,-1,0};
 s->cr=light->color[0];s->cg=light->color[1];s->cb=light->color[2];
 s->range=light->position[3]>0.1f?light->position[3]:10.0f;
 s->cos=light->color[3]>0.0f?light->color[3]:0.8f;s->on=1;
 if(i==0){float outer=acosf(s->cos);Vector3 target={s->pos.x+s->dir.x,s->pos.y+s->dir.y,s->pos.z+s->dir.z};
  Matrix view=MatrixLookAt(s->pos,target,(Vector3){0,1,0});
  Matrix proj=MatrixPerspective(outer*2.0f,1.0f,0.1f,s->range);g_spot2VP=MatrixMultiply(proj,view);g_spot2On=1;}
 else if(i==1){float outer=acosf(s->cos);Vector3 target={s->pos.x+s->dir.x,s->pos.y+s->dir.y,s->pos.z+s->dir.z};
  Matrix view=MatrixLookAt(s->pos,target,(Vector3){0,1,0});
  Matrix proj=MatrixPerspective(outer*2.0f,1.0f,0.1f,s->range);g_spot3VP=MatrixMultiply(proj,view);}
 else if(i==2){float outer=acosf(s->cos);Vector3 target={s->pos.x+s->dir.x,s->pos.y+s->dir.y,s->pos.z+s->dir.z};
  Matrix view=MatrixLookAt(s->pos,target,(Vector3){0,1,0});
  Matrix proj=MatrixPerspective(outer*2.0f,1.0f,0.1f,s->range);g_spot4VP=MatrixMultiply(proj,view);}
 return i;
}
void vgraphics_ClearLights(void){memset(g_extraSpots,0,sizeof g_extraSpots);memset(&g_points[1],0,sizeof(g_points[1])*3);g_spot2On=0;}
int vgraphics_GetLightCount(void){int n=g_spotOn?1:0;for(int i=0;i<3;++i)if(g_extraSpots[i].on)++n;for(int i=1;i<4;++i)if(g_points[i].on)++n;return n;}
void vgraphics_SetFog(bool e,Color c,float start,float end,float density){(void)e;(void)c;(void)start;(void)end;(void)density;}
bool vgraphics_internal_recreate(void){cfg();return true;}
#endif
