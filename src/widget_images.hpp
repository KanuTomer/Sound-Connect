#pragma once
#include <windows.h>
#include <wincodec.h>
#include <winrt/base.h>
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <array>

static std::string png_uri(std::span<const uint8_t> bytes){
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result="data:image/png;base64,";uint32_t buffer{};int bits{};
    for(auto value:bytes){buffer=(buffer<<8)|value;bits+=8;while(bits>=6){bits-=6;result+=alphabet[(buffer>>bits)&63];}}
    if(bits)result+=alphabet[(buffer<<(6-bits))&63];while((result.size()-22)%4)result+='=';return result;
}
static std::string bitmap_uri(int width,int height,std::span<const uint8_t> pixels){
    winrt::com_ptr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.put())));
    winrt::com_ptr<IStream> stream;winrt::check_hresult(CreateStreamOnHGlobal(nullptr,TRUE,stream.put()));
    winrt::com_ptr<IWICBitmapEncoder> encoder;winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.put()));winrt::check_hresult(encoder->Initialize(stream.get(),WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame;winrt::check_hresult(encoder->CreateNewFrame(frame.put(),nullptr));winrt::check_hresult(frame->Initialize(nullptr));winrt::check_hresult(frame->SetSize(width,height));
    auto format=GUID_WICPixelFormat32bppBGRA;winrt::check_hresult(frame->SetPixelFormat(&format));if(format!=GUID_WICPixelFormat32bppBGRA)throw std::runtime_error("PNG encoder changed the pixel format.");
    winrt::check_hresult(frame->WritePixels(height,width*4,static_cast<UINT>(pixels.size()),const_cast<BYTE*>(pixels.data())));winrt::check_hresult(frame->Commit());winrt::check_hresult(encoder->Commit());
    STATSTG stat{};winrt::check_hresult(stream->Stat(&stat,STATFLAG_NONAME));if(stat.cbSize.QuadPart>65536)throw std::runtime_error("Widget artwork exceeded its size limit.");
    HGLOBAL global{};winrt::check_hresult(GetHGlobalFromStream(stream.get(),&global));auto* bytes=static_cast<uint8_t*>(GlobalLock(global));if(!bytes)winrt::throw_last_error();
    auto result=png_uri({bytes,static_cast<size_t>(stat.cbSize.QuadPart)});GlobalUnlock(global);return result;
}
// Flat fills contain no corners or borders that the host can crop.
static std::string solid_image(COLORREF color,BYTE alpha=255){std::array<uint8_t,4> pixel{GetBValue(color),GetGValue(color),GetRValue(color),alpha};return bitmap_uri(1,1,pixel);}
static std::string line_image(COLORREF color){constexpr int width=1072,height=4;std::vector<uint8_t> pixels(width*height*4);for(int i=0;i<width*height;++i){pixels[i*4]=GetBValue(color);pixels[i*4+1]=GetGValue(color);pixels[i*4+2]=GetRValue(color);pixels[i*4+3]=255;}return bitmap_uri(width,height,pixels);}
static std::string button_surface(bool dark,bool high){return solid_image(high?GetSysColor(COLOR_WINDOW):dark?RGB(61,63,65):RGB(241,240,238));}
static std::string divider_image(bool dark,bool high){return solid_image(high?GetSysColor(COLOR_WINDOWTEXT):dark?RGB(74,76,78):RGB(222,222,220));}
static std::string border_image(bool dark,bool high){return line_image(high?GetSysColor(COLOR_WINDOWTEXT):dark?RGB(88,90,92):RGB(209,208,206));}
// A complete frame preserves all four edges in the Windows renderer. Its
// translucent interior lets the host's selectAction hover remain visible.
static std::string button_frame(int logical_width,int logical_height,bool selected,bool dark,bool high){
    constexpr int scale=4;int width=logical_width*scale,height=logical_height*scale;
    std::vector<uint8_t> pixels(width*height*4);
    auto border=high?GetSysColor(COLOR_WINDOWTEXT):dark?RGB(88,90,92):RGB(209,208,206);
    auto accent=high?GetSysColor(COLOR_HIGHLIGHT):dark?RGB(76,194,255):RGB(0,103,192);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        float px=(x+.5f)/scale,py=(y+.5f)/scale;
        auto rounded=[&](float inset,float radius){float dx=std::max(inset+radius-px,px-(logical_width-inset-radius)),dy=std::max(inset+radius-py,py-(logical_height-inset-radius));return std::sqrt(std::max(dx,0.f)*std::max(dx,0.f)+std::max(dy,0.f)*std::max(dy,0.f))+std::min(std::max(dx,dy),0.f)<=radius;};
        if(!rounded(0,2))continue;
        bool edge=!rounded(1,1),marker=selected&&py>=logical_height-2;
        auto color=marker?accent:edge?border:dark?RGB(255,255,255):RGB(0,0,0);
        auto at=(y*width+x)*4;pixels[at]=GetBValue(color);pixels[at+1]=GetGValue(color);pixels[at+2]=GetRValue(color);pixels[at+3]=edge||marker?255:high?0:dark?16:8;
    }
    return bitmap_uri(width,height,pixels);
}
static auto button_frame_artwork(bool high){
    std::map<int,std::pair<std::string,std::string>> result;
    for(auto [width,height]:{std::pair{28,28},std::pair{130,44},std::pair{85,46},std::pair{85,62}})
        for(bool selected:{false,true}){int key=height+(selected?1000:0);result[key]={button_frame(width,height,selected,false,high),button_frame(width,height,selected,true,high)};}
    return result;
}
static std::string battery_readout_image(int value,int index,int size,bool dark,bool high){
    if(value<0||value>20)return {};
    constexpr int scale=4,width=80*scale;int logical_height=size==0?16:size==1?21:25,height=logical_height*scale,font_size=size==0?12:size==1?17:20;
    std::wstring label=value==0&&index<2?L"Off":std::to_wstring(value)+L"%";if(size==0)label=std::wstring(index==0?L"Left ":index==1?L"Right ":L"Case ")+label;
    auto color=high?GetSysColor(COLOR_WINDOWTEXT):dark?RGB(255,154,147):RGB(179,41,36);
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)winrt::throw_last_error();BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* raw{};
    HBITMAP mask=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);HFONT font=CreateFontW(-font_size*scale,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if(!mask||!font){if(mask)DeleteObject(mask);if(font)DeleteObject(font);DeleteDC(dc);throw std::runtime_error("Battery readout could not be created.");}
    auto old_bitmap=SelectObject(dc,mask),old_font=SelectObject(dc,font);std::fill_n(static_cast<uint8_t*>(raw),width*height*4,0);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));RECT rect{0,0,width,height};DrawTextW(dc,label.c_str(),-1,&rect,DT_SINGLELINE|DT_NOPREFIX|DT_LEFT|DT_VCENTER);GdiFlush();
    std::vector<uint8_t> pixels(width*height*4);auto* bytes=static_cast<uint8_t*>(raw);for(int i=0;i<width*height;++i){pixels[i*4]=GetBValue(color);pixels[i*4+1]=GetGValue(color);pixels[i*4+2]=GetRValue(color);pixels[i*4+3]=std::max({bytes[i*4],bytes[i*4+1],bytes[i*4+2]});}
    SelectObject(dc,old_bitmap);SelectObject(dc,old_font);DeleteObject(mask);DeleteObject(font);DeleteDC(dc);return bitmap_uri(width,height,pixels);
}
static auto battery_readout_artwork(const std::array<int,3>& values,bool high){
    std::map<std::string,std::array<std::pair<std::string,std::string>,3>> result;const char* sizes[]={"small","medium","large"};
    for(int size=0;size<3;++size)for(int i=0;i<3;++i)result[sizes[size]][i]={battery_readout_image(values[i],i,size,false,high),battery_readout_image(values[i],i,size,true,high)};return result;
}
static std::string battery_image(int value,bool,bool dark,bool high){
    constexpr int width=320,height=12;std::vector<uint8_t> pixels(width*height*4);
    bool valid=value>=0&&value<=100,low=valid&&value<=20;
    auto track=high?GetSysColor(COLOR_WINDOWTEXT):dark?RGB(74,76,78):RGB(222,222,220);
    auto fill=high?GetSysColor(COLOR_WINDOWTEXT):low?(dark?RGB(255,154,147):RGB(179,41,36)):(dark?RGB(194,195,196):RGB(97,97,97));
    int edge=valid?static_cast<int>(width*value/100.f):0;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){bool ink=x<edge;auto color=ink?fill:track;auto at=(y*width+x)*4;pixels[at]=GetBValue(color);pixels[at+1]=GetGValue(color);pixels[at+2]=GetRValue(color);pixels[at+3]=!valid||(high&&!ink)?80:255;}
    return bitmap_uri(width,height,pixels);
}
static std::string selection_marker(bool dark,bool high){return line_image(high?GetSysColor(COLOR_HIGHLIGHT):dark?RGB(76,194,255):RGB(0,103,192));}
static std::string switch_image(bool on,bool dark,bool high){
    // Square checkbox geometry agrees with the native rectangular hover target.
    constexpr int width=96,height=96;std::vector<uint8_t> pixels(width*height*4);
    auto ink=high?GetSysColor(COLOR_WINDOWTEXT):dark?RGB(175,178,195):RGB(103,106,128);
    auto accent=high?GetSysColor(COLOR_HIGHLIGHT):dark?RGB(76,194,255):RGB(0,103,192);
    auto tick=high?GetSysColor(COLOR_HIGHLIGHTTEXT):dark?RGB(37,33,59):RGB(255,255,255);
    auto distance=[](float x,float y,float x0,float y0,float x1,float y1){float dx=x1-x0,dy=y1-y0,t=std::clamp(((x-x0)*dx+(y-y0)*dy)/(dx*dx+dy*dy),0.f,1.f);return std::sqrt((x-x0-t*dx)*(x-x0-t*dx)+(y-y0-t*dy)*(y-y0-t*dy));};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){if(x<8||x>=88||y<8||y>=88)continue;bool edge=x<14||x>=82||y<14||y>=82;if(!on&&!edge)continue;auto color=on?accent:ink;
        if(on&&std::min(distance(x+.5f,y+.5f,26,48,43,65),distance(x+.5f,y+.5f,43,65,72,32))<4)color=tick;
        auto at=(y*width+x)*4;pixels[at]=GetBValue(color);pixels[at+1]=GetGValue(color);pixels[at+2]=GetRValue(color);pixels[at+3]=255;
    }return bitmap_uri(width,height,pixels);
}
// Render each graph at its final aspect ratio, at 4x resolution. No bitmap is
// stretched into a different graph shape by the Widgets host.
static std::string curve_image(std::span<const int32_t> bands,COLORREF accent,bool dark,bool high_contrast,int logical_width=268,int logical_height=88){
    if(bands.size()!=5||logical_width<32||logical_height<16)return {};
    constexpr int scale=4;const int width=logical_width*scale,height=logical_height*scale;
    std::vector<uint8_t> pixels(width*height*4);
    auto blend=[&](int x,int y,COLORREF color,float opacity){
        if(x<0||x>=width||y<0||y>=height||opacity<=0)return;opacity=std::min(1.f,opacity);auto offset=(y*width+x)*4;
        float old=pixels[offset+3]/255.f,alpha=opacity+old*(1-opacity);
        const int channels[]={GetBValue(color),GetGValue(color),GetRValue(color)};
        for(int i=0;i<3;++i)pixels[offset+i]=static_cast<uint8_t>((channels[i]*opacity+pixels[offset+i]*old*(1-opacity))/alpha+.5f);
        pixels[offset+3]=static_cast<uint8_t>(alpha*255+.5f);
    };
    auto stroke=[&](float x0,float y0,float x1,float y1,COLORREF color,float alpha,float thickness){
        x0*=scale;y0*=scale;x1*=scale;y1*=scale;float radius=thickness*scale/2,dx=x1-x0,dy=y1-y0,length=dx*dx+dy*dy;
        int left=std::max(0,static_cast<int>(std::floor(std::min(x0,x1)-radius-1))),right=std::min(width-1,static_cast<int>(std::ceil(std::max(x0,x1)+radius+1)));
        int top=std::max(0,static_cast<int>(std::floor(std::min(y0,y1)-radius-1))),bottom=std::min(height-1,static_cast<int>(std::ceil(std::max(y0,y1)+radius+1)));
        for(int y=top;y<=bottom;++y)for(int x=left;x<=right;++x){float px=x+.5f,py=y+.5f,t=length?std::clamp(((px-x0)*dx+(py-y0)*dy)/length,0.f,1.f):0;
            float ex=px-x0-t*dx,ey=py-y0-t*dy,coverage=std::clamp(radius+.5f-std::sqrt(ex*ex+ey*ey),0.f,1.f);blend(x,y,color,alpha*coverage);
        }
    };
    auto neutral=dark?RGB(194,195,196):RGB(97,97,97),ink=dark?RGB(244,244,244):RGB(32,32,32);
    if(high_contrast){neutral=ink=GetSysColor(COLOR_WINDOWTEXT);accent=GetSysColor(COLOR_HIGHLIGHT);}
    bool medium=logical_height<64;float zero=medium?26.f:38.f,unit=medium?.7f:2.f;
    float xs[5]={36,90,144,198,252},ys[5]{};for(int i=0;i<5;++i)ys[i]=zero-std::clamp(bands[i],-10,10)*unit;
    for(int value:{-10,0,10})stroke(28,zero-value*unit,260,zero-value*unit,neutral,high_contrast?1.f:value==0?.32f:.17f,.7f);
    for(int i=0;i<5;++i){if(i)stroke(xs[i-1],ys[i-1],xs[i],ys[i],accent,1.f,1.5f);stroke(xs[i],ys[i],xs[i],ys[i],accent,1.f,4.6f);}
    // Render readable labels into the same 4x transparent image, keeping graph
    // and labels at exactly the same aspect ratio in every Windows host.
    HDC dc=CreateCompatibleDC(nullptr);if(!dc)winrt::throw_last_error();
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* raw{};HBITMAP mask=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&raw,nullptr,0);
    HFONT font=CreateFontW(-11*scale,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if(!mask||!font){if(mask)DeleteObject(mask);if(font)DeleteObject(font);DeleteDC(dc);throw std::runtime_error("Graph labels could not be created.");}
    auto old_bitmap=SelectObject(dc,mask),old_font=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));
    auto clear=[&]{std::fill_n(static_cast<uint8_t*>(raw),width*height*4,0);};
    auto label=[&](const std::wstring& value,float x,float baseline,bool centered){RECT rect{static_cast<LONG>((centered?x-18:x)*scale),static_cast<LONG>((baseline-11)*scale),static_cast<LONG>((centered?x+18:x+28)*scale),static_cast<LONG>((baseline+3)*scale)};DrawTextW(dc,value.c_str(),-1,&rect,DT_SINGLELINE|DT_NOPREFIX|(centered?DT_CENTER:DT_LEFT));};
    auto apply=[&](COLORREF color){GdiFlush();auto* bytes=static_cast<uint8_t*>(raw);for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto at=(y*width+x)*4;auto coverage=std::max({bytes[at],bytes[at+1],bytes[at+2]});if(coverage)blend(x,y,color,coverage/255.f);}};
    clear();if(!medium){label(L"+10",0,22,false);label(L"0",0,42,false);label(L"−10",0,62,false);const wchar_t* frequencies[]={L"400",L"1k",L"2.5k",L"6.3k",L"16k"};for(int i=0;i<5;++i)label(frequencies[i],xs[i],82,true);}apply(neutral);
    clear();for(int i=0;i<5;++i)label((bands[i]>0?L"+":L"")+std::to_wstring(bands[i]),xs[i],ys[i]-8,true);apply(ink);
    SelectObject(dc,old_bitmap);SelectObject(dc,old_font);DeleteObject(mask);DeleteObject(font);DeleteDC(dc);
    return bitmap_uri(width,height,pixels);
}

static std::map<std::string,std::pair<std::string,std::string>> curve_artwork(std::span<const int32_t> bands,bool high){
    std::map<std::string,std::pair<std::string,std::string>> result;
    result["large"]={curve_image(bands,RGB(0,103,192),false,high,268,88),curve_image(bands,RGB(76,194,255),true,high,268,88)};
    result["summary"]=result["large"];
    result["medium"]={curve_image(bands,RGB(0,103,192),false,high,268,44),curve_image(bands,RGB(76,194,255),true,high,268,44)};
    return result;
}
