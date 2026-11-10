#include "Assets/TextureImport.h"
#include "Assets/AssetManager.h"
#include "Core/FileSystem.h"
#include "Assets/AssetMeta.h"
namespace Stardust {
TextureImportSettings TextureImportSettings::Load(const std::string& asset){TextureImportSettings s;auto j=AssetMeta::Read(asset,"texture");if(j.empty())return s;try{s.type=j.value("type",0);s.maxSize=std::clamp(j.value("maxSize",4096),16,16384);s.filter=j.value("filter",2);s.wrap=j.value("wrap",1);s.srgb=j.value("srgb",true)&&s.type!=1;s.mipmaps=j.value("mipmaps",true);s.readable=j.value("readable",false);}catch(...){}return s;}
bool TextureImportSettings::Save(const std::string& asset)const{return AssetMeta::Write(asset,"texture",nlohmann::json({{"type",type},{"maxSize",maxSize},{"filter",filter},{"wrap",wrap},{"srgb",srgb},{"mipmaps",mipmaps},{"readable",readable}}));}
RHI::SamplerIndex TextureImportSettings::Sampler()const{return filter==0?(wrap?RHI::SamplerIndex::PointWrap:RHI::SamplerIndex::PointClamp):filter==2&&wrap?RHI::SamplerIndex::AnisoWrap:wrap?RHI::SamplerIndex::LinearWrap:RHI::SamplerIndex::LinearClamp;}
}
