// Families are selected from complete relocated function/reference graphs.
static constexpr char pattern_player_finished[]="488b411080b87401000000740c80b990000000007403b001c332c0c3";
namespace abi1013 {
#include "native/profiles/profile_1013.inc"
}
namespace abi0901 {
#include "native/profiles/profile_0901.inc"
}
namespace abi0805 {
#include "native/profiles/profile_0805.inc"
}
namespace abi1226 {
#include "native/profiles/profile_1226.inc"
}
namespace abi0113 {
#include "native/profiles/profile_0113.inc"
}
namespace abi0324 {
#include "native/profiles/profile_0324.inc"
}
namespace abi0714 {
#include "native/profiles/profile_0714.inc"
}
namespace abi0825 {
#include "native/profiles/profile_0825.inc"
}
namespace abi0203 {
#include "native/profiles/profile_0203.inc"
}
namespace abi0217 {
#include "native/profiles/profile_0217.inc"
}
namespace abi1209 {
#include "native/profiles/profile_1209.inc"
}
static const Family catalog[]={
    {"sdvx-eg-abi1",abi1013::anchors,world_1013},
    {"sdvx-eg-abi0901",abi0901::anchors,world_0901},
    {"sdvx-eg-abi0805",abi0805::anchors,world_0805},
    {"sdvx-nabla-1226",abi1226::anchors,world_1226,result_1226,false},
    {"sdvx-nabla-0113",abi0113::anchors,world_1226,result_early,false},
    {"sdvx-nabla-0203",abi0203::anchors,world_1226,result_early,false},
    {"sdvx-nabla-0217",abi0217::anchors,world_1226,result_early,false},
    {"sdvx-nabla-0324",abi0324::anchors,world_1226,result_early,false},
    {"sdvx-nabla-0714",abi0714::anchors,world_1226,{},false},
    {"sdvx-nabla-0825",abi0825::anchors,world_0901},
    {"sdvx-eg-1209",abi1209::anchors,world_1209,result_1209,false,true},
};
