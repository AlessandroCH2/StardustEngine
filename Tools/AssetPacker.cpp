#include "Assets/Pak.h"
#include "Project/BuildCooker.h"
#include <iostream>
int main(int argc, char** argv) {
    if(argc==4&&std::string(argv[1])=="cook"){if(!Stardust::Project::Load(argv[2]))return 1;return Stardust::BuildCooker::Cook(Stardust::Project::Dir(),argv[3],Stardust::Project::Config())?0:1;}
    if (argc != 4 || std::string(argv[1]) != "build") {
        std::cerr<<"Usage: StardustPacker build <asset-folder> <output.pak>\n";
        return 2;
    }
    return Stardust::Pak::Build(argv[2],argv[3]) ? 0 : 1;
}
