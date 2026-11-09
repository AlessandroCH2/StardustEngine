#include "Core/WindowIcon.h"
#include "Core/FileSystem.h"
#include "Core/Log.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
#include <stb_image.h>
#include <algorithm>
#include <cstring>
#include <climits>
#include <vector>

namespace Stardust::WindowIcon {
namespace {
struct Image {
    int width=0,height=0;
    std::vector<unsigned char> pixels;
};
bool Load(const std::filesystem::path& path,Image& image) {
    auto bytes=FileSystem::ReadBinary(path);
    if(bytes.empty()||bytes.size()>INT_MAX)return false;
    int channels=0;
    auto* pixels=stbi_load_from_memory(bytes.data(),(int)bytes.size(),&image.width,&image.height,&channels,4);
    if(!pixels)return false;
    image.pixels.assign(pixels,pixels+(size_t)image.width*image.height*4);
    stbi_image_free(pixels);return true;
}
Image Resize(const Image& source,int size) {
    Image result{size,size,std::vector<unsigned char>((size_t)size*size*4)};
    float scale=(float)size/std::max(source.width,source.height);
    int width=std::max(1,(int)(source.width*scale)),height=std::max(1,(int)(source.height*scale));
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        float sx=std::max(0.f,(x+.5f)*source.width/width-.5f),sy=std::max(0.f,(y+.5f)*source.height/height-.5f);
        int x0=std::min((int)sx,source.width-1),y0=std::min((int)sy,source.height-1);
        int x1=std::min(x0+1,source.width-1),y1=std::min(y0+1,source.height-1);
        float fx=sx-x0,fy=sy-y0;
        auto dst=((size_t)(y+(size-height)/2)*size+x+(size-width)/2)*4;
        // Interpolate premultiplied alpha to avoid dark fringes on transparent icons.
        float alpha=0,colors[3]{};
        for(int j=0;j<2;++j)for(int i=0;i<2;++i){
            auto src=((size_t)(j?y1:y0)*source.width+(i?x1:x0))*4;
            float weight=(i?fx:1-fx)*(j?fy:1-fy),a=source.pixels[src+3]/255.f;
            alpha+=weight*a;
            for(int c=0;c<3;++c)colors[c]+=weight*a*source.pixels[src+c];
        }
        result.pixels[dst+3]=(unsigned char)(std::clamp(alpha,0.f,1.f)*255+.5f);
        for(int c=0;c<3;++c)result.pixels[dst+c]=alpha>0?(unsigned char)std::clamp(colors[c]/alpha,0.f,255.f):0;
    }
    return result;
}
#ifdef _WIN32
#pragma pack(push,2)
struct GroupHeader { WORD reserved=0,type=1,count=6; };
struct GroupEntry { BYTE width,height,colors=0,reserved=0;WORD planes=1,bits=32;DWORD bytes;WORD id; };
#pragma pack(pop)
#endif
}

void ApplyEmbedded(GLFWwindow* window) {
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) return; // Wayland windows take their icon from the desktop entry
#ifdef _WIN32
    auto hwnd=glfwGetWin32Window(window);
    auto module=GetModuleHandleW(nullptr);
    // Shared module-owned handles remain valid for the lifetime of the process.
    for(auto [which,size]:{std::pair{ICON_BIG,32},std::pair{ICON_SMALL,16}})
        if(auto icon=LoadImageW(module,MAKEINTRESOURCEW(101),IMAGE_ICON,size,size,LR_SHARED))
            SendMessageW(hwnd,WM_SETICON,which,(LPARAM)icon);
#else
    Apply(window,FileSystem::ExecutableDir()/"engineIcon.png");
#endif
}
bool Apply(GLFWwindow* window,const std::filesystem::path& path) {
    Image source;if(!Load(path,source)){SD_WARN("Cannot load window icon: {}",path.string());return false;}
    auto small=Resize(source,32),large=Resize(source,256);
    GLFWimage images[]={{small.width,small.height,small.pixels.data()},{large.width,large.height,large.pixels.data()}};
    glfwSetWindowIcon(window,2,images);return true;
}
bool Embed(const std::filesystem::path& executable,const std::filesystem::path& path) {
#ifndef _WIN32
    return false;
#else
    Image source;if(!Load(path,source)){SD_ERROR("Cannot load player icon: {}",path.string());return false;}
    HANDLE update=BeginUpdateResourceW(executable.c_str(),FALSE);
    if(!update)return false;
    std::vector<unsigned char> group(sizeof(GroupHeader)+6*sizeof(GroupEntry));
    GroupHeader header;std::memcpy(group.data(),&header,sizeof(header));
    int index=0;bool success=true;
    for(int size:{16,32,48,64,128,256}) {
        auto image=Resize(source,size);
        size_t maskStride=((size+31)/32)*4;
        std::vector<unsigned char> data(sizeof(BITMAPINFOHEADER)+(size_t)size*size*4+maskStride*size);
        BITMAPINFOHEADER bitmap{};bitmap.biSize=sizeof(bitmap);bitmap.biWidth=size;bitmap.biHeight=size*2;
        bitmap.biPlanes=1;bitmap.biBitCount=32;bitmap.biSizeImage=size*size*4;
        std::memcpy(data.data(),&bitmap,sizeof(bitmap));
        for(int y=0;y<size;++y)for(int x=0;x<size;++x){
            auto src=((size_t)y*size+x)*4,dst=sizeof(bitmap)+((size_t)(size-1-y)*size+x)*4;
            data[dst]=image.pixels[src+2];data[dst+1]=image.pixels[src+1];data[dst+2]=image.pixels[src];data[dst+3]=image.pixels[src+3];
            if(!image.pixels[src+3])data[sizeof(bitmap)+(size_t)size*size*4+(size-1-y)*maskStride+x/8]|=0x80>>(x%8);
        }
        GroupEntry entry{};entry.width=entry.height=size==256?0:(BYTE)size;entry.bytes=(DWORD)data.size();entry.id=(WORD)(201+index);
        std::memcpy(group.data()+sizeof(header)+index*sizeof(entry),&entry,sizeof(entry));
        success=success&&UpdateResourceW(update,MAKEINTRESOURCEW(3),MAKEINTRESOURCEW(entry.id),MAKELANGID(LANG_NEUTRAL,SUBLANG_NEUTRAL),data.data(),(DWORD)data.size());++index;
    }
    success=success&&UpdateResourceW(update,MAKEINTRESOURCEW(14),MAKEINTRESOURCEW(101),MAKELANGID(LANG_NEUTRAL,SUBLANG_NEUTRAL),group.data(),(DWORD)group.size());
    if(!EndUpdateResourceW(update,!success))success=false;
    return success;
#endif
}
}
