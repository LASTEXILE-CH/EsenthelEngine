/******************************************************************************/
#include "!Header.h"
/******************************************************************************/
#define MacroFrom   192.0
#define MacroTo     320.0
#define MacroMax    0.70
#define MacroScale (1.0/32)

#define DEFAULT_TEX_SIZE 1024.0 // 1024x1024

#define PARALLAX_MODE 1 // 1=best

#define RELIEF_STEPS_MAX    32
#define RELIEF_STEPS_BINARY 3 // 3 works good in most cases, 4 could be used for absolute best quality
#define RELIEF_STEPS_MUL    0.75 // 0.75 gets slightly worse quality but better performance, 1.0 gets full quality but slower performance, default=0.75
#define RELIEF_LOD_OFFSET   0.33 // values >0 increase performance (by using fewer steps and smaller LODs) which also makes results more soft and flat helping to reduce jitter for distant pixels, default=0.33
#define RELIEF_TAN_POS      1 // 0=gets worse quality but better performance (not good for triangles with vertexes with very different normals or for surfaces very close to camera), 1=gets full quality but slower performance, default=1
#define RELIEF_DEC_NRM      1 // if reduce relief bump intensity where there are big differences between vtx normals, tangents and binormals, default=1
#define RELIEF_MODE         1 // 1=best
#define RELIEF_Z_LIMIT      0.4 // smaller values may cause leaking (UV swimming), and higher reduce bump intensity at angles, default=0.4
#define RELIEF_LOD_TEST     0 // close to camera (test enabled=4.76 fps, test disabled=4.99 fps), far from camera (test enabled=9.83 fps, test disabled=9.52 fps), conclusion: this test reduces performance when close to camera by a similar factor to when far away, however since more likely pixels will be close to camera (as for distant LODs other shaders are used) we prioritize close to camera performance, so this check should be disabled, default=0

#define FAST_TPOS  ((BUMP_MODE>=SBUMP_PARALLAX_MIN && BUMP_MODE<=SBUMP_PARALLAX_MAX) || (BUMP_MODE==SBUMP_RELIEF && !RELIEF_TAN_POS))
#define GRASS_FADE (FX==FX_GRASS_2D || FX==FX_GRASS_3D)

#define USE_VEL 1
#define TESSELATE_VEL (TESSELATE && USE_VEL && 0) // FIXME

#define SET_POS (TESSELATE || MACRO || (!FAST_TPOS && BUMP_MODE>SBUMP_FLAT))
#define SET_TEX (LAYOUT || DETAIL || MACRO || BUMP_MODE>SBUMP_FLAT)
/******************************************************************************
SKIN, MATERIALS, LAYOUT, BUMP_MODE, ALPHA_TEST, DETAIL, MACRO, COLORS, MTRL_BLEND, HEIGHTMAP, FX, TESSELATE
/******************************************************************************/
struct VS_PS
{
#if SET_TEX
   Vec2 tex:TEXCOORD;
#endif

#if SET_POS
   Vec pos:POS;
#endif

#if USE_VEL
   Vec projected_prev_pos_xyw:PREV_POS;
#endif
#if TESSELATE_VEL
   VecH nrm_prev:PREV_NORMAL;
#endif

#if   BUMP_MODE> SBUMP_FLAT
   MatrixH3 mtrx:MATRIX; // !! may not be Normalized !!
   VecH Nrm() {return mtrx[2];}
#elif BUMP_MODE==SBUMP_FLAT
   VecH nrm:NORMAL; // !! may not be Normalized !!
   VecH Nrm() {return nrm;}
#else
   VecH Nrm() {return 0;}
#endif

#if MATERIALS>1
   VecH4 material:MATERIAL;
#endif

#if COLORS
   VecH col:COLOR;
#endif

#if FAST_TPOS
   Vec _tpos:TPOS;
   Vec  tpos() {return Normalize(_tpos);}
#elif BUMP_MODE>SBUMP_FLAT
   Vec  tpos() {return Normalize(TransformTP(-pos, mtrx));} // need high precision here for 'TransformTP'
#else
   Vec  tpos() {return 0;}
#endif

#if GRASS_FADE
   Half fade_out:FADE_OUT;
#endif
};
/******************************************************************************/
// VS
/******************************************************************************/
void VS
(
   VtxInput vtx,

   out VS_PS O,
   out Vec4  O_vtx:POSITION,

   CLIP_DIST
)
{
   Vec  local_pos=vtx.pos(), view_pos;
   VecH nrm, tan; if(BUMP_MODE>=SBUMP_FLAT)nrm=vtx.nrm(); if(BUMP_MODE>SBUMP_FLAT)tan=vtx.tan(nrm, HEIGHTMAP);

   // VEL
   Vec local_pos_prev, view_pos_prev; VecH nrm_prev; if(USE_VEL){local_pos_prev=local_pos; if(TESSELATE_VEL)nrm_prev=nrm;}

#if SET_TEX
   O.tex=vtx.tex(HEIGHTMAP);
   if(HEIGHTMAP && MATERIALS==1)O.tex*=Material.tex_scale;
#endif

#if MATERIALS>1
   O.material=vtx.material();
#endif

#if COLORS
   if(MATERIALS<=1)O.col=vtx.colorFast3()*Material.color.rgb;
   else            O.col=vtx.colorFast3();
#endif

   if(FX==FX_LEAF_2D || FX==FX_LEAF_3D)
   {
      if(BUMP_MODE> SBUMP_FLAT)BendLeaf(vtx.hlp(), local_pos, nrm, tan);else
      if(BUMP_MODE==SBUMP_FLAT)BendLeaf(vtx.hlp(), local_pos, nrm     );else
                               BendLeaf(vtx.hlp(), local_pos          );
      if(USE_VEL)
      {
         if(TESSELATE_VEL)BendLeaf(vtx.hlp(), local_pos_prev, nrm_prev, true);else
                          BendLeaf(vtx.hlp(), local_pos_prev          , true);
      }
   }
   if(FX==FX_LEAFS_2D || FX==FX_LEAFS_3D)
   {
      if(BUMP_MODE> SBUMP_FLAT)BendLeafs(vtx.hlp(), vtx.size(), local_pos, nrm, tan);else
      if(BUMP_MODE==SBUMP_FLAT)BendLeafs(vtx.hlp(), vtx.size(), local_pos, nrm     );else
                               BendLeafs(vtx.hlp(), vtx.size(), local_pos          );
      if(USE_VEL)
      {
         if(TESSELATE_VEL)BendLeafs(vtx.hlp(), vtx.size(), local_pos_prev, nrm_prev, true);else
                          BendLeafs(vtx.hlp(), vtx.size(), local_pos_prev          , true);
      }
   }

   if(!SKIN)
   {
      if(true) // instance
      {
                    view_pos     =TransformPos    (local_pos     , vtx.instance());
         if(USE_VEL)view_pos_prev=TransformPosPrev(local_pos_prev, vtx.instance());

      #if   BUMP_MODE> SBUMP_FLAT
         O.mtrx[2]=TransformDir(nrm, vtx.instance());
         O.mtrx[0]=TransformDir(tan, vtx.instance());
      #elif BUMP_MODE==SBUMP_FLAT
         O.nrm    =TransformDir(nrm, vtx.instance());
      #endif
      #if TESSELATE_VEL
         O.nrm_prev=TransformDirPrev(nrm_prev, vtx.instance());
      #endif

         if(FX==FX_GRASS_2D || FX==FX_GRASS_3D)
         {
                       BendGrass(local_pos     , view_pos     , vtx.instance());
            if(USE_VEL)BendGrass(local_pos_prev, view_pos_prev, vtx.instance(), true);
         }
      #if GRASS_FADE
         O.fade_out=GrassFadeOut(vtx.instance());
      #endif
      }else
      {
                    view_pos     =TransformPos    (local_pos);
         if(USE_VEL)view_pos_prev=TransformPosPrev(local_pos_prev);

      #if   BUMP_MODE> SBUMP_FLAT
         O.mtrx[2]=TransformDir(nrm);
         O.mtrx[0]=TransformDir(tan);
      #elif BUMP_MODE==SBUMP_FLAT
         O.nrm    =TransformDir(nrm);
      #endif
      #if TESSELATE_VEL
         O.nrm_prev=TransformDirPrev(nrm_prev);
      #endif

         if(FX==FX_GRASS_2D || FX==FX_GRASS_3D)
         {
                       BendGrass(local_pos     , view_pos);
            if(USE_VEL)BendGrass(local_pos_prev, view_pos_prev, 0, true);
         }
      #if GRASS_FADE
         O.fade_out=GrassFadeOut();
      #endif
      }
   }else
   {
      VecU bone    =vtx.bone  ();
      VecH weight_h=vtx.weight();
                 view_pos     =TransformPos    (local_pos     , bone, vtx.weight());
      if(USE_VEL)view_pos_prev=TransformPosPrev(local_pos_prev, bone, vtx.weight());

   #if   BUMP_MODE> SBUMP_FLAT
      O.mtrx[2]=TransformDir(nrm, bone, weight_h);
      O.mtrx[0]=TransformDir(tan, bone, weight_h);
   #elif BUMP_MODE==SBUMP_FLAT
      O.nrm    =TransformDir(nrm, bone, weight_h);
   #endif
   #if TESSELATE_VEL
      O.nrm_prev=TransformDirPrev(nrm_prev, bone, weight_h);
   #endif
   }

   // normalize (have to do all at the same time, so all have the same lengths)
   if(BUMP_MODE> SBUMP_FLAT // calculating binormal (this also covers the case when we have tangent from heightmap which is not Normalized)
   || BUMP_MODE==SBUMP_RELIEF && RELIEF_DEC_NRM // needed for RELIEF_DEC_NRM effect
   || TESSELATE) // needed for tesselation
   {
   #if   BUMP_MODE> SBUMP_FLAT
      O.mtrx[2]=Normalize(O.mtrx[2]);
      O.mtrx[0]=Normalize(O.mtrx[0]);
   #elif BUMP_MODE==SBUMP_FLAT
      O.nrm    =Normalize(O.nrm);
   #endif
   #if TESSELATE_VEL
      O.nrm_prev=Normalize(O.nrm_prev);
   #endif
   }

#if BUMP_MODE>SBUMP_FLAT
   O.mtrx[1]=vtx.bin(O.mtrx[2], O.mtrx[0], HEIGHTMAP);
#endif

#if FAST_TPOS
   O._tpos=TransformTP(-view_pos, O.mtrx); // need high precision here, we can't Normalize because it's important to keep distances
#endif

#if SET_POS
   O.pos=view_pos;
#endif
   O_vtx=Project(view_pos); CLIP_PLANE(view_pos);
#if USE_VEL
   O.projected_prev_pos_xyw=ProjectPrevXYW(view_pos_prev);
#endif
}
/******************************************************************************/
// PS
/******************************************************************************/
void PS
(
   VS_PS I,
#if USE_VEL
   PIXEL,
#endif
#if FX!=FX_GRASS_2D && FX!=FX_LEAF_2D && FX!=FX_LEAFS_2D
   IS_FRONT,
#endif

   out DeferredSolidOutput output
)
{
   VecH col, nrm;
   Half smooth, reflect, glow;

#if COLORS
   col=I.col;
#else
   if(MATERIALS<=1)col=Material.color.rgb;
#endif

#if MATERIALS==1
   // apply tex coord bump offset
   #if BUMP_MODE>=SBUMP_PARALLAX_MIN && BUMP_MODE<=SBUMP_PARALLAX_MAX // Parallax
   {
      const Int steps=BUMP_MODE-SBUMP_PARALLAX0;

      VecH tpos=I.tpos();
   #if   PARALLAX_MODE==0 // too flat
      Half scale=Material.bump/steps;
   #elif PARALLAX_MODE==1 // best results (not as flat, and not much aliasing)
      Half scale=Material.bump/(steps*Lerp(1, tpos.z, tpos.z)); // Material.bump/steps/Lerp(1, tpos.z, tpos.z);
   #elif PARALLAX_MODE==2 // generates too steep walls (not good for parallax)
      Half scale=Material.bump/(steps*Lerp(1, tpos.z, Sat(tpos.z/0.5))); // Material.bump/steps/Lerp(1, tpos.z, tpos.z);
   #elif PARALLAX_MODE==3 // introduces a bit too much aliasing/artifacts on surfaces perpendicular to view direction
      Half scale=Material.bump/steps*(2-tpos.z); // Material.bump/steps*Lerp(1, 1/tpos.z, tpos.z)
   #else // correct however introduces way too much aliasing/artifacts on surfaces perpendicular to view direction
      Half scale=Material.bump/(steps*tpos.z);
   #endif
      tpos.xy*=scale; VecH2 add=-0.5*tpos.xy;
      UNROLL for(Int i=0; i<steps; i++)I.tex+=Tex(BUMP_IMAGE, I.tex).BUMP_CHANNEL*tpos.xy+add; // (tex-0.5)*tpos.xy = tex*tpos.xy + -0.5*tpos.xy
   }
   #elif BUMP_MODE==SBUMP_RELIEF // Relief
   {
   #if RELIEF_LOD_TEST
      BRANCH if(GetLod(I.tex, DEFAULT_TEX_SIZE)<=4)
   #endif
      {
      #if !GL
         Vec2 TexSize; BUMP_IMAGE.GetDimensions(TexSize.x, TexSize.y);
      #else
         Flt TexSize=DEFAULT_TEX_SIZE; // on GL using 'GetDimensions' would force create a secondary sampler for 'BUMP_IMAGE'
      #endif
         Flt lod=Max(0, GetLod(I.tex, TexSize)+RELIEF_LOD_OFFSET); // yes, can be negative, so use Max(0) to avoid increasing number of steps when surface is close to camera
         //lod=Trunc(lod); don't do this as it would reduce performance and generate more artifacts, with this disabled, we generate fewer steps gradually, and blend with the next MIP level softening results

         VecH tpos=I.tpos();
      #if   RELIEF_MODE==0
         Half scale=Material.bump;
      #elif RELIEF_MODE==1 // best
         Half scale=Material.bump/Lerp(1, tpos.z, Sat(tpos.z/RELIEF_Z_LIMIT));
      #elif RELIEF_MODE==2 // produces slight aliasing/artifacts on surfaces perpendicular to view direction
         Half scale=Material.bump/Max(tpos.z, RELIEF_Z_LIMIT);
      #else // correct however introduces way too much aliasing/artifacts on surfaces perpendicular to view direction
         Half scale=Material.bump/tpos.z;
      #endif

      #if RELIEF_DEC_NRM
         scale*=Length2(I.mtrx[0])*Length2(I.mtrx[1])*Length2(I.mtrx[2]); // vtx matrix vectors are interpolated linearly, which means that if there are big differences between vtx vectors, then their length will be smaller and smaller, for example if #0 vtx normal is (1,0), and #1 vtx normal is (0,1), then interpolated value between them will be (0.5, 0.5)
      #endif
         tpos.xy*=-scale;

         Flt length=Length(tpos.xy) * TexSize.x / exp2(lod); // how many pixels, Pow(2, lod)=exp2(lod)
         if(RELIEF_STEPS_MUL!=1)if(lod>0)length*=RELIEF_STEPS_MUL; // don't use this for first LOD

         I.tex-=tpos.xy*0.5;

         Int  steps   =Mid(length, 0, RELIEF_STEPS_MAX);
         Half stp     =1.0/(steps+1),
              ray     =1;
         Vec2 tex_step=tpos.xy*stp; // keep as HP to avoid conversions several times in the loop below

      #if 1 // linear + interval search (faster)
         // linear search
         Half height_next, height_prev=0.5; // use 0.5 as approximate average value, we could do "TexLodI(BUMP_IMAGE, I.tex, lod).BUMP_CHANNEL", however in tests that wasn't needed but only reduced performance
         LOOP for(Int i=0; ; i++)
         {
            ray  -=stp;
            I.tex+=tex_step;
            height_next=TexLodI(BUMP_IMAGE, I.tex, lod).BUMP_CHANNEL;
            if(i>=steps || height_next>=ray)break;
            height_prev=height_next;
         }

         // interval search
         if(1)
         {
            Half ray_prev=ray+stp;
            // prev pos: I.tex-tex_step, height_prev-ray_prev
            // next pos: I.tex         , height_next-ray
            Half hn=height_next-ray,
                 hp=height_prev-ray_prev,
                 frac=Sat(hn/(hn-hp));
            I.tex-=tex_step*frac;

            BRANCH if(lod<=0) // extra step (needed only for closeup)
            {
               Half ray_cur=ray+stp*frac,
                    height_cur=TexLodI(BUMP_IMAGE, I.tex, lod).BUMP_CHANNEL;
               if(  height_cur>=ray_cur) // if still below, then have to go back more, lerp between this position and prev pos
               {
                  // prev pos: I.tex-tex_step (BUT I.tex before adjustment), height_prev-ray_prev
                  // next pos: I.tex                                       , height_cur -ray_cur
                  tex_step*=1-frac; // we've just travelled "tex_step*frac", so to go to the prev point, we need what's left, "tex_step*(1-frac)"
               }else // we went back too far, go forward, lerp between next pos and this position
               {
                  // prev pos: I.tex                              , height_cur -ray_cur
                  // next pos: I.tex (BUT I.tex before adjustment), height_next-ray
                  hp=hn;
                  tex_step*=-frac; // we've just travelled "tex_step*frac", so to go to the next point, we need the same way but other direction, "tex_step*-frac"
               }
               hn=height_cur-ray_cur;
               frac=Sat(hn/(hn-hp));
               I.tex-=tex_step*frac;
            }
         }
      #else // linear + binary search (slower because requires 3 tex reads in binary to get the same results as with only 0-1 tex reads in interval)
         // linear search
         LOOP for(Int i=0; ; i++)
         {
            ray  -=stp;
            I.tex+=tex_step;
            if(i>=steps || TexLodI(BUMP_IMAGE, I.tex, lod).BUMP_CHANNEL>=ray)break;
         }

         // binary search
         {
            Half ray_prev=ray+stp,
                 l=0, r=1, m=0.5;
            UNROLL for(Int i=0; i<RELIEF_STEPS_BINARY; i++)
            {
               Half height=TexLodI(BUMP_IMAGE, I.tex-tex_step*m, lod).BUMP_CHANNEL;
               if(  height>Lerp(ray, ray_prev, m))l=m;else r=m;
               m=Avg(l, r);
            }
            I.tex-=tex_step*m;
         }
      #endif
      }
   }
   #endif

   VecH4 det;
#if DETAIL
   det=GetDetail(I.tex);
#endif

   // #MaterialTextureLayout
   #if LAYOUT==0
   {
      smooth =Material.smooth;
      reflect=Material.reflect;
      glow   =Material.glow;
      if(DETAIL){col*=det.z; smooth*=det.w;}
   }
   #elif LAYOUT==1
   {
      VecH4 tex_col=Tex(Col, I.tex);
      if(ALPHA_TEST)
      {
      #if GRASS_FADE
         tex_col.a-=I.fade_out;
      #endif
         AlphaTest(tex_col.a);
      }
      col   *=tex_col.rgb;
      smooth =Material.smooth;
      reflect=Material.reflect;
      glow   =Material.glow;
      if(DETAIL){col*=det.z; smooth*=det.w;}
   }
   #elif LAYOUT==2
   {
      VecH4 tex_col=Tex(Col, I.tex);
      if(ALPHA_TEST)
      {
      #if GRASS_FADE
         tex_col.a-=I.fade_out;
      #endif
         AlphaTest(tex_col.a);
      }
      VecH4 tex_ext=Tex(Ext, I.tex);
      col   *=tex_col.rgb;
      smooth =Material.smooth *tex_ext.x;
      reflect=Material.reflect*tex_ext.y;
      glow   =Material.glow   *tex_ext.w;
      if(DETAIL){col*=det.z; smooth*=det.w;}
   }
   #endif

#if MACRO
   col=Lerp(col, Tex(Mac, I.tex*MacroScale).rgb, LerpRS(MacroFrom, MacroTo, Length(I.pos))*MacroMax);
#endif

   // normal
#if   BUMP_MODE==SBUMP_ZERO
   nrm=0;
#elif BUMP_MODE==SBUMP_FLAT
   nrm=Normalize(I.Nrm()); // can't add DETAIL normal because it would need 'I.mtrx'
#else
             nrm.xy =Tex(Nrm, I.tex).xy*Material.normal;
   if(DETAIL)nrm.xy+=det.xy;
             nrm.z  =CalcZ(nrm.xy);
             nrm    =Normalize(Transform(nrm, I.mtrx));
   // TODO: a better formula would be: nrm.xy=Tex(Nrm, I.tex).xy; nrm.z=CalcZ(nrm.xy); nrm.xy*=Material.normal; nrm=Normalize(Transform(nrm, I.mtrx)); however it gets more complicated/slower with DETAIL and multi-materials, we could do this if GPU's are super fast
#endif

#else // MATERIALS>1
   // assuming that in multi materials LAYOUT!=0
   Vec2 tex0, tex1, tex2, tex3;
                   tex0=I.tex*MultiMaterial0.tex_scale;
                   tex1=I.tex*MultiMaterial1.tex_scale;
   if(MATERIALS>=3)tex2=I.tex*MultiMaterial2.tex_scale;
   if(MATERIALS>=4)tex3=I.tex*MultiMaterial3.tex_scale;

   // apply tex coord bump offset
   #if BUMP_MODE>=SBUMP_PARALLAX_MIN && BUMP_MODE<=SBUMP_PARALLAX_MAX // Parallax
   {
      const Int steps=BUMP_MODE-SBUMP_PARALLAX0;

      VecH tpos=I.tpos();
   #if   PARALLAX_MODE==0 // too flat
      Half scale=(1.0/steps);
   #elif PARALLAX_MODE==1 // best results (not as flat, and not much aliasing)
      Half scale=(1.0/steps)/Lerp(1, tpos.z, tpos.z);
   #elif PARALLAX_MODE==2 // generates too steep walls (not good for parallax)
      Half scale=(1.0/steps)/Lerp(1, tpos.z, Sat(tpos.z/0.5));
   #elif PARALLAX_MODE==3 // introduces a bit too much aliasing/artifacts on surfaces perpendicular to view direction
      Half scale=(1.0/steps)*(2-tpos.z); // 1/steps*Lerp(1, 1/tpos.z, tpos.z)
   #else // correct however introduces way too much aliasing/artifacts on surfaces perpendicular to view direction
      Half scale=(1.0/steps)/tpos.z;
   #endif
      tpos.xy*=scale;

      // h=(tex-0.5)*bump_mul = x*bump_mul - 0.5*bump_mul
      VecH4 bump_mul; bump_mul.x=MultiMaterial0.bump;
      VecH4 bump_add; bump_mul.y=MultiMaterial1.bump; if(MATERIALS==2){bump_mul.xy  *=I.material.xy  ; bump_add.xy  =bump_mul.xy  *-0.5;}
      if(MATERIALS>=3)bump_mul.z=MultiMaterial2.bump; if(MATERIALS==3){bump_mul.xyz *=I.material.xyz ; bump_add.xyz =bump_mul.xyz *-0.5;}
      if(MATERIALS>=4)bump_mul.w=MultiMaterial3.bump; if(MATERIALS==4){bump_mul.xyzw*=I.material.xyzw; bump_add.xyzw=bump_mul.xyzw*-0.5;}

      UNROLL for(Int i=0; i<steps; i++) // I.tex+=h*tpos.xy;
      {
                    Half h =Tex(       BUMP_IMAGE    , tex0).BUMP_CHANNEL*bump_mul.x+bump_add.x;
                         h+=Tex(CONCAT(BUMP_IMAGE, 1), tex1).BUMP_CHANNEL*bump_mul.y+bump_add.y;
         if(MATERIALS>=3)h+=Tex(CONCAT(BUMP_IMAGE, 2), tex2).BUMP_CHANNEL*bump_mul.z+bump_add.z;
         if(MATERIALS>=4)h+=Tex(CONCAT(BUMP_IMAGE, 3), tex3).BUMP_CHANNEL*bump_mul.w+bump_add.w;

         Vec2 offset=h*tpos.xy; // keep as HP to avoid multiple conversions below

                         tex0+=offset;
                         tex1+=offset;
         if(MATERIALS>=3)tex2+=offset;
         if(MATERIALS>=4)tex3+=offset;
      }
   }
   #elif BUMP_MODE==SBUMP_RELIEF // Relief
   {
   #if RELIEF_LOD_TEST
      BRANCH if(GetLod(I.tex, DEFAULT_TEX_SIZE)<=4)
   #endif
      {
         VecH4 bump_mul; bump_mul.x=MultiMaterial0.bump; Half avg_bump;
                         bump_mul.y=MultiMaterial1.bump; if(MATERIALS==2){bump_mul.xy  *=I.material.xy  ; avg_bump=Sum(bump_mul.xy  );} // use 'Sum' because they're premultipled by 'I.material'
         if(MATERIALS>=3)bump_mul.z=MultiMaterial2.bump; if(MATERIALS==3){bump_mul.xyz *=I.material.xyz ; avg_bump=Sum(bump_mul.xyz );}
         if(MATERIALS>=4)bump_mul.w=MultiMaterial3.bump; if(MATERIALS==4){bump_mul.xyzw*=I.material.xyzw; avg_bump=Sum(bump_mul.xyzw);}

         Flt TexSize=DEFAULT_TEX_SIZE, // here we have 2..4 textures, so use a default value
                 lod=Max(0, GetLod(I.tex, TexSize)+RELIEF_LOD_OFFSET); // yes, can be negative, so use Max(0) to avoid increasing number of steps when surface is close to camera
         //lod=Trunc(lod); don't do this as it would reduce performance and generate more artifacts, with this disabled, we generate fewer steps gradually, and blend with the next MIP level softening results

         VecH tpos=I.tpos();
      #if   RELIEF_MODE==0
         Half scale=avg_bump;
      #elif RELIEF_MODE==1 // best
         Half scale=avg_bump/Lerp(1, tpos.z, Sat(tpos.z/RELIEF_Z_LIMIT));
      #elif RELIEF_MODE==2 // produces slight aliasing/artifacts on surfaces perpendicular to view direction
         Half scale=avg_bump/Max(tpos.z, RELIEF_Z_LIMIT);
      #else // correct however introduces way too much aliasing/artifacts on surfaces perpendicular to view direction
         Half scale=avg_bump/tpos.z;
      #endif

      #if RELIEF_DEC_NRM
         scale*=Length2(I.mtrx[0])*Length2(I.mtrx[1])*Length2(I.mtrx[2]); // vtx matrix vectors are interpolated linearly, which means that if there are big differences between vtx vectors, then their length will be smaller and smaller, for example if #0 vtx normal is (1,0), and #1 vtx normal is (0,1), then interpolated value between them will be (0.5, 0.5)
      #endif
         tpos.xy*=-scale;

         Flt length=Length(tpos.xy) * TexSize.x / exp2(lod); // how many pixels, Pow(2, lod)=exp2(lod)
         if(RELIEF_STEPS_MUL!=1)if(lod>0)length*=RELIEF_STEPS_MUL; // don't use this for first LOD

         //I.tex-=tpos.xy*0.5;
         Vec2 offset=tpos.xy*0.5; // keep as HP to avoid multiple conversions below
                         tex0-=offset;
                         tex1-=offset;
         if(MATERIALS>=3)tex2-=offset;
         if(MATERIALS>=4)tex3-=offset;

         Int  steps   =Mid(length, 0, RELIEF_STEPS_MAX);
         Half stp     =1.0/(steps+1),
              ray     =1;
         Vec2 tex_step=tpos.xy*stp; // keep as HP to avoid conversions several times in the loop below

      #if 1 // linear + interval search (faster)
         // linear search
         Half height_next, height_prev=0.5; // use 0.5 as approximate average value, we could do "TexLodI(BUMP_IMAGE, I.tex, lod).BUMP_CHANNEL", however in tests that wasn't needed but only reduced performance
         LOOP for(Int i=0; ; i++)
         {
            ray-=stp;

            //I.tex+=tex_step;
                            tex0+=tex_step;
                            tex1+=tex_step;
            if(MATERIALS>=3)tex2+=tex_step;
            if(MATERIALS>=4)tex3+=tex_step;

            //height_next=TexLodI(BUMP_IMAGE, I.tex, lod).BUMP_CHANNEL;
                            height_next =TexLodI(       BUMP_IMAGE    , tex0, lod).BUMP_CHANNEL*I.material.x;
                            height_next+=TexLodI(CONCAT(BUMP_IMAGE, 1), tex1, lod).BUMP_CHANNEL*I.material.y;
            if(MATERIALS>=3)height_next+=TexLodI(CONCAT(BUMP_IMAGE, 2), tex2, lod).BUMP_CHANNEL*I.material.z;
            if(MATERIALS>=4)height_next+=TexLodI(CONCAT(BUMP_IMAGE, 3), tex3, lod).BUMP_CHANNEL*I.material.w;

            if(i>=steps || height_next>=ray)break;
            height_prev=height_next;
         }

         // interval search
         if(1)
         {
            Half ray_prev=ray+stp;
            // prev pos: I.tex-tex_step, height_prev-ray_prev
            // next pos: I.tex         , height_next-ray
            Half hn=height_next-ray, hp=height_prev-ray_prev,
               frac=Sat(hn/(hn-hp));

            //I.tex-=tex_step*frac;
            offset=tex_step*frac;
                            tex0-=offset;
                            tex1-=offset;
            if(MATERIALS>=3)tex2-=offset;
            if(MATERIALS>=4)tex3-=offset;

            BRANCH if(lod<=0) // extra step (needed only for closeup)
            {
               Half ray_cur=ray+stp*frac,
                             //height_cur =TexLodI(       BUMP_IMAGE    ,I.tex, lod).BUMP_CHANNEL;
                               height_cur =TexLodI(       BUMP_IMAGE    , tex0, lod).BUMP_CHANNEL*I.material.x;
                               height_cur+=TexLodI(CONCAT(BUMP_IMAGE, 1), tex1, lod).BUMP_CHANNEL*I.material.y;
               if(MATERIALS>=3)height_cur+=TexLodI(CONCAT(BUMP_IMAGE, 2), tex2, lod).BUMP_CHANNEL*I.material.z;
               if(MATERIALS>=4)height_cur+=TexLodI(CONCAT(BUMP_IMAGE, 3), tex3, lod).BUMP_CHANNEL*I.material.w;

               if(height_cur>=ray_cur) // if still below, then have to go back more, lerp between this position and prev pos
               {
                  // prev pos: I.tex-tex_step (BUT I.tex before adjustment), height_prev-ray_prev
                  // next pos: I.tex                                       , height_cur -ray_cur
                  tex_step*=1-frac; // we've just travelled "tex_step*frac", so to go to the prev point, we need what's left, "tex_step*(1-frac)"
               }else // we went back too far, go forward, lerp between next pos and this position
               {
                  // prev pos: I.tex                              , height_cur -ray_cur
                  // next pos: I.tex (BUT I.tex before adjustment), height_next-ray
                  hp=hn;
                  tex_step*=-frac; // we've just travelled "tex_step*frac", so to go to the next point, we need the same way but other direction, "tex_step*-frac"
               }
               hn=height_cur-ray_cur;
               frac=Sat(hn/(hn-hp));

             //I.tex-=tex_step*frac;
               offset=tex_step*frac;
                               tex0-=offset;
                               tex1-=offset;
               if(MATERIALS>=3)tex2-=offset;
               if(MATERIALS>=4)tex3-=offset;
            }
         }
      #else // linear + binary search (slower because requires 3 tex reads in binary to get the same results as with only 0-1 tex reads in interval)
         this needs to be updated for 4 materials

         // linear search
         LOOP for(Int i=0; ; i++)
         {
            ray  -=stp;
            I.tex+=tex_step;
            if(i>=steps || TexLodI(BUMP_IMAGE, I.tex, lod).BUMP_CHANNEL>=ray)break;
         }

         // binary search
         {
            Half ray_prev=ray+stp,
                 l=0, r=1, m=0.5;
            UNROLL for(Int i=0; i<RELIEF_STEPS_BINARY; i++)
            {
               Half height=TexLodI(BUMP_IMAGE, I.tex-tex_step*m, lod).BUMP_CHANNEL;
               if(  height>Lerp(ray, ray_prev, m))l=m;else r=m;
               m=Avg(l, r);
            }
            I.tex-=tex_step*m;
         }
      #endif
      }
   }
   #endif // Relief

   // #MaterialTextureLayout

   // detail texture
   VecH4 det0, det1, det2, det3;
   if(DETAIL)
   {
                      det0=GetDetail0(tex0);
                      det1=GetDetail1(tex1);
      if(MATERIALS>=3)det2=GetDetail2(tex2);
      if(MATERIALS>=4)det3=GetDetail3(tex3);
   }

   // macro texture
   Half mac_blend;
#if MACRO
   mac_blend=LerpRS(MacroFrom, MacroTo, Length(I.pos))*MacroMax;
#endif

   // Smooth, Reflect, Bump, Glow !! DO THIS FIRST because it may modify 'I.material' which affects everything !!
   VecH srg; // smooth_reflect_glow
   if(LAYOUT==2)
   {
      VecH4 ext0, ext1, ext2, ext3;
                      ext0=Tex(Ext , tex0);
                      ext1=Tex(Ext1, tex1);
      if(MATERIALS>=3)ext2=Tex(Ext2, tex2);
      if(MATERIALS>=4)ext3=Tex(Ext3, tex3);
      if(MTRL_BLEND)
      {
                          I.material.x=MultiMaterialWeight(I.material.x, ext0.BUMP_CHANNEL);
                          I.material.y=MultiMaterialWeight(I.material.y, ext1.BUMP_CHANNEL); if(MATERIALS==2)I.material.xy  /=I.material.x+I.material.y;
         if(MATERIALS>=3){I.material.z=MultiMaterialWeight(I.material.z, ext2.BUMP_CHANNEL); if(MATERIALS==3)I.material.xyz /=I.material.x+I.material.y+I.material.z;}
         if(MATERIALS>=4){I.material.w=MultiMaterialWeight(I.material.w, ext3.BUMP_CHANNEL); if(MATERIALS==4)I.material.xyzw/=I.material.x+I.material.y+I.material.z+I.material.w;}
      }
                      {VecH srg0=ext0.xyw*MultiMaterial0.srg_mul+MultiMaterial0.srg_add; if(DETAIL)srg0.x*=det0.w; srg =srg0*I.material.x;}
                      {VecH srg1=ext1.xyw*MultiMaterial1.srg_mul+MultiMaterial1.srg_add; if(DETAIL)srg1.x*=det1.w; srg+=srg1*I.material.y;}
      if(MATERIALS>=3){VecH srg2=ext2.xyw*MultiMaterial2.srg_mul+MultiMaterial2.srg_add; if(DETAIL)srg2.x*=det2.w; srg+=srg2*I.material.z;}
      if(MATERIALS>=4){VecH srg3=ext3.xyw*MultiMaterial3.srg_mul+MultiMaterial3.srg_add; if(DETAIL)srg3.x*=det3.w; srg+=srg3*I.material.w;}
   }else
   {
                      {VecH srg0=MultiMaterial0.srg_add; if(DETAIL)srg0.x*=det0.w; srg =srg0*I.material.x;}
                      {VecH srg1=MultiMaterial1.srg_add; if(DETAIL)srg1.x*=det1.w; srg+=srg1*I.material.y;}
      if(MATERIALS>=3){VecH srg2=MultiMaterial2.srg_add; if(DETAIL)srg2.x*=det2.w; srg+=srg2*I.material.z;}
      if(MATERIALS>=4){VecH srg3=MultiMaterial3.srg_add; if(DETAIL)srg3.x*=det3.w; srg+=srg3*I.material.w;}
   }
   smooth =srg.x;
   reflect=srg.y;
   glow   =srg.z;

   // Color + Detail + Macro !! do this second after modifying 'I.material' !! here Alpha is ignored for multi-materials
   VecH rgb;
                   {VecH col0=Tex(Col , tex0).rgb; col0.rgb*=MultiMaterial0.color.rgb; if(DETAIL)col0.rgb*=det0.z; if(MACRO)col0.rgb=Lerp(col0.rgb, Tex(Mac , tex0*MacroScale).rgb, MultiMaterial0.macro*mac_blend); rgb =I.material.x*col0;}
                   {VecH col1=Tex(Col1, tex1).rgb; col1.rgb*=MultiMaterial1.color.rgb; if(DETAIL)col1.rgb*=det1.z; if(MACRO)col1.rgb=Lerp(col1.rgb, Tex(Mac1, tex1*MacroScale).rgb, MultiMaterial1.macro*mac_blend); rgb+=I.material.y*col1;}
   if(MATERIALS>=3){VecH col2=Tex(Col2, tex2).rgb; col2.rgb*=MultiMaterial2.color.rgb; if(DETAIL)col2.rgb*=det2.z; if(MACRO)col2.rgb=Lerp(col2.rgb, Tex(Mac2, tex2*MacroScale).rgb, MultiMaterial2.macro*mac_blend); rgb+=I.material.z*col2;}
   if(MATERIALS>=4){VecH col3=Tex(Col3, tex3).rgb; col3.rgb*=MultiMaterial3.color.rgb; if(DETAIL)col3.rgb*=det3.z; if(MACRO)col3.rgb=Lerp(col3.rgb, Tex(Mac3, tex3*MacroScale).rgb, MultiMaterial3.macro*mac_blend); rgb+=I.material.w*col3;}
#if COLORS
   col*=rgb.rgb;
#else
   col =rgb.rgb;
#endif

   // normal
#if   BUMP_MODE==SBUMP_ZERO
   nrm=0;
#elif BUMP_MODE==SBUMP_FLAT
   nrm=Normalize(I.Nrm()); // can't add DETAIL normal because it would need 'I.mtrx'
#else
   if(DETAIL)
   {
                      nrm.xy =(Tex(Nrm , tex0).xy*MultiMaterial0.normal + det0.xy)*I.material.x;
                      nrm.xy+=(Tex(Nrm1, tex1).xy*MultiMaterial1.normal + det1.xy)*I.material.y;
      if(MATERIALS>=3)nrm.xy+=(Tex(Nrm2, tex2).xy*MultiMaterial2.normal + det2.xy)*I.material.z;
      if(MATERIALS>=4)nrm.xy+=(Tex(Nrm3, tex3).xy*MultiMaterial3.normal + det3.xy)*I.material.w;
   }else
   {
                      nrm.xy =Tex(Nrm , tex0).xy*(MultiMaterial0.normal*I.material.x);
                      nrm.xy+=Tex(Nrm1, tex1).xy*(MultiMaterial1.normal*I.material.y);
      if(MATERIALS>=3)nrm.xy+=Tex(Nrm2, tex2).xy*(MultiMaterial2.normal*I.material.z);
      if(MATERIALS>=4)nrm.xy+=Tex(Nrm3, tex3).xy*(MultiMaterial3.normal*I.material.w);
   }
   nrm.z=CalcZ(nrm.xy);
   nrm  =Normalize(Transform(nrm, I.mtrx));
#endif

#endif // MATERIALS

   col+=Highlight.rgb;

#if FX!=FX_GRASS_2D && FX!=FX_LEAF_2D && FX!=FX_LEAFS_2D
   BackFlip(nrm, front);
#endif

   output.color       (col    );
   output.glow        (glow   );
   output.normal      (nrm    );
   output.translucent (FX==FX_GRASS_3D || FX==FX_LEAF_3D || FX==FX_LEAFS_3D);
   output.smooth      (smooth );
   output.reflect     (reflect);
#if USE_VEL
   output.velocity    (I.projected_prev_pos_xyw, pixel);
#else
   output.velocityZero();
#endif
}
/******************************************************************************/
// HULL / DOMAIN
/******************************************************************************/
#if TESSELATE
HSData HSConstant(InputPatch<VS_PS,3> I) {return GetHSData(I[0].pos, I[1].pos, I[2].pos, I[0].Nrm(), I[1].Nrm(), I[2].Nrm());}
[maxtessfactor(5.0)]
[domain("tri")]
[partitioning("fractional_odd")] // use 'odd' because it supports range from 1.0 ('even' supports range from 2.0)
[outputtopology("triangle_cw")]
[patchconstantfunc("HSConstant")]
[outputcontrolpoints(3)]
VS_PS HS
(
   InputPatch<VS_PS,3> I,
   UInt cp_id:SV_OutputControlPointID
)
{
   VS_PS O;

   O.pos=I[cp_id].pos;

#if USE_VEL
   O.projected_prev_pos_xyw=I[cp_id].projected_prev_pos_xyw;
#endif

#if TESSELATE_VEL
   O.nrm_prev=I[cp_id].nrm_prev;
#endif

#if SET_TEX
   O.tex=I[cp_id].tex;
#endif

#if   BUMP_MODE> SBUMP_FLAT
   O.mtrx=I[cp_id].mtrx;
#elif BUMP_MODE==SBUMP_FLAT
   O.nrm =I[cp_id].nrm ;
#endif

#if MATERIALS>1
   O.material=I[cp_id].material;
#endif

#if COLORS
   O.col=I[cp_id].col;
#endif

#if FAST_TPOS
   O._tpos=I[cp_id]._tpos;
#endif

#if GRASS_FADE
   O.fade_out=I[cp_id].fade_out;
#endif

   return O;
}
/******************************************************************************/
[domain("tri")]
void DS
(
   HSData hs_data, const OutputPatch<VS_PS,3> I, Vec B:SV_DomainLocation,

   out VS_PS O,
   out Vec4  O_vtx:POSITION
)
{
#if   BUMP_MODE> SBUMP_FLAT
   O.mtrx[0]=I[0].mtrx[0]*B.z + I[1].mtrx[0]*B.x + I[2].mtrx[0]*B.y;
   O.mtrx[1]=I[0].mtrx[1]*B.z + I[1].mtrx[1]*B.x + I[2].mtrx[1]*B.y;
   SetDSPosNrm(O.pos, O.mtrx[2], I[0].pos, I[1].pos, I[2].pos, I[0].Nrm(), I[1].Nrm(), I[2].Nrm(), B, hs_data, false, 0);
#elif BUMP_MODE==SBUMP_FLAT
   SetDSPosNrm(O.pos, O.nrm    , I[0].pos, I[1].pos, I[2].pos, I[0].Nrm(), I[1].Nrm(), I[2].Nrm(), B, hs_data, false, 0);
#endif

#if SET_TEX
   O.tex=I[0].tex*B.z + I[1].tex*B.x + I[2].tex*B.y;
#endif

#if MATERIALS>1
   O.material=I[0].material*B.z + I[1].material*B.x + I[2].material*B.y;
#endif

#if COLORS
   O.col=I[0].col*B.z + I[1].col*B.x + I[2].col*B.y;
#endif

#if FAST_TPOS
   O._tpos=I[0]._tpos*B.z + I[1]._tpos*B.x + I[2]._tpos*B.y;
#endif

#if GRASS_FADE
   O.fade_out=I[0].fade_out*B.z + I[1].fade_out*B.x + I[2].fade_out*B.y;
#endif

   O_vtx=Project(O.pos);

#if USE_VEL
   #if TESSELATE_VEL
      FIXME
   #else // this is just an approximation
      Vec interpolated_pos    =I[0].pos                   *B.z + I[1].pos                   *B.x + I[2].pos                   *B.y;
      O.projected_prev_pos_xyw=I[0].projected_prev_pos_xyw*B.z + I[1].projected_prev_pos_xyw*B.x + I[2].projected_prev_pos_xyw*B.y
                              +O_vtx.xyw-ProjectXYW(interpolated_pos); // + delta (tesselated pos - interpolated pos)
   #endif
#endif
}
#endif
/******************************************************************************/
