#pragma once
#include <stdint.h>
#include <stddef.h>
static inline uint8_t jpeg_clip(int v){return v<0?0:v>255?255:(uint8_t)v;}
static inline uint16_t jpeg_rgb565(uint8_t y,uint8_t u,uint8_t v)
{
    int cb=(int)u-128,cr=(int)v-128;
    unsigned r=jpeg_clip((int)y+((1436*cr+512)>>10));
    unsigned g=jpeg_clip((int)y-((352*cb+731*cr+512)>>10));
    unsigned b=jpeg_clip((int)y+((1815*cb+512)>>10));
    return (uint16_t)((r>>3)<<11|(g>>2)<<5|(b>>3));
}
/* Cedar MB32 Y: 32x32 tiles. UV: 32-byte rows, 32 chroma rows
 * per tile; a UV tile spans two vertical luma tiles. */
static inline uint16_t jpeg_mb32_pixel(const uint8_t *yp,const uint8_t *uv,
                                      uint32_t stride,uint32_t x,uint32_t y)
{
    uint32_t columns=stride/32,tx=x/32,ty=y/32;
    size_t yi=((size_t)ty*columns+tx)*1024+(y%32)*32+x%32;
    size_t ci=((size_t)(y/64)*columns+tx)*1024+((y/2)%32)*32+(x%32/2)*2;
    return jpeg_rgb565(yp[yi],uv[ci],uv[ci+1]);
}
static inline void jpeg_mb32_scale(uint16_t *out,uint32_t dw,uint32_t dh,
                                   const uint8_t *y,const uint8_t *uv,
                                   uint32_t stride,uint32_t width,uint32_t height)
{
    for(uint32_t row=0;row<dh;++row){
        uint32_t sy=(uint64_t)row*height/dh;
        for(uint32_t col=0;col<dw;++col)
            out[(size_t)row*dw+col]=jpeg_mb32_pixel(y,uv,stride,(uint64_t)col*width/dw,sy);
    }
}

/* Board-verified raw JPEG layout: 0=MB32 (default). Linear YV12=1,
 * NV12=2 and I420=3 remain explicit diagnostic comparisons only. */
static inline void jpeg_preview_scale(uint16_t *out,uint32_t dw,uint32_t dh,
                                       const uint8_t *base,uint32_t stride,uint32_t padded_h,
                                       uint32_t width,uint32_t height,int layout)
{
    const uint8_t *chroma=base+(size_t)stride*padded_h;
    if(layout==0){jpeg_mb32_scale(out,dw,dh,base,chroma,stride,width,height);return;}
    size_t plane=(size_t)stride*padded_h/4;
    for(uint32_t row=0;row<dh;++row){
        uint32_t sy=(uint64_t)row*height/dh;
        for(uint32_t col=0;col<dw;++col){
            uint32_t sx=(uint64_t)col*width/dw;
            size_t ci=(size_t)(sy/2)*(stride/2)+sx/2;
            uint8_t u,v;
            if(layout==2){ci=(size_t)(sy/2)*stride+(sx/2)*2;u=chroma[ci];v=chroma[ci+1];}
            else if(layout==3){u=chroma[ci];v=chroma[plane+ci];}
            else{v=chroma[ci];u=chroma[plane+ci];}
            out[(size_t)row*dw+col]=jpeg_rgb565(base[(size_t)sy*stride+sx],u,v);
        }
    }
}
