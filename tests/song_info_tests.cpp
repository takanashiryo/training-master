#include "core/song_info.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void write(const std::filesystem::path& path,const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path,std::ios::binary); stream.write(bytes.data(),bytes.size());
    check(stream.good(),"could not write fixture");
}
std::string sjis(const std::wstring& text) {
    const int size=WideCharToMultiByte(932,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string bytes(size,0);
    WideCharToMultiByte(932,0,text.data(),static_cast<int>(text.size()),bytes.data(),size,nullptr,nullptr);
    return bytes;
}
std::string png() {
    const std::string hex="89504E470D0A1A0A0000000D4948445200000002000000010806000000F4227F8A000000017352474200AECE1CE90000000467414D410000B18F0BFC6105000000097048597300000EC300000EC301C76FA8640000001149444154185763386124F25F6EDA9DFF00120B049D5A4B7BD70000000049454E44AE426082";
    const auto digit=[](char c) { return c<='9'?c-'0':c-'A'+10; };
    std::string bytes;
    for (std::size_t i=0;i<hex.size();i+=2) bytes+=static_cast<char>(digit(hex[i])*16+digit(hex[i+1]));
    return bytes;
}
}
int wmain(int argc,wchar_t** argv) {
    if (argc==2) {
        training::SongCatalog catalog; std::string error;
        if (!catalog.load(argv[1],error)) { std::cerr<<error<<'\n'; return 1; }
        const auto info=catalog.lookup(1767,4,"/data/music/1767_mixxon_misoilepunch/1767_mixxon_misoilepunch_5m.vox");
        check(info->title=="MixxioN","real catalog title mismatch");
        check(info->artist=="MisoilePunch♪","real catalog Shift-JIS artist mismatch");
        check(info->difficulty=="MAXIMUM  Lv. 20.6","real catalog level conversion mismatch");
        check(info->jacket.filename()==L"jk_1767_5.png","real catalog selected jacket mismatch");
        const auto image=training::load_jacket(info->jacket);
        check(!image.bgra.empty() && image.width<=256 && image.height<=256,"real jacket decode failed");
        std::cout<<"Real catalog: "<<catalog.size()<<" songs; selected jacket decoded\n"; return 0;
    }
    const auto root=std::filesystem::temp_directory_path()/(L"training-song-info-test-"+std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path,ec); } } cleanup{root};
    try {
        const auto database=root/L"data/others/music_db.xml";
        write(database,sjis(L"<?xml version=\"1.0\" encoding=\"shift-jis\"?><mdb><music id=\"7\"><info>"
            L"<title_name>音楽 &amp; Loop</title_name><artist_name>テスト♪</artist_name><ascii>test</ascii><inf_ver>3</inf_ver>"
            L"</info><difficulty><novice><difnum>50</difnum></novice><infinite><difnum>187</difnum></infinite>"
            L"<maximum><difnum>206</difnum></maximum></difficulty></music></mdb>"));
        const auto base=root/L"data/music/0007_test/jk_0007_1.png", selected=root/L"data/music/0007_test/jk_0007_4.png";
        write(base,png()); write(selected,png());
        training::SongCatalog catalog; std::string error;
        check(catalog.load(root,error),"Shift-JIS catalog load failed");
        const auto infinite=catalog.lookup(7,3,"/data/music/0007_test/test_4i.vox");
        check(infinite->title=="音楽 & Loop" && infinite->artist=="テスト♪","Shift-JIS/entities must produce UTF-8 metadata");
        check(infinite->difficulty=="GRAVITY  Lv. 18.7","special difficulty or fractional level mismatch");
        check(infinite->jacket==selected,"selected difficulty jacket must precede base");
        check(catalog.lookup(7,4,"")->jacket==base,"missing selected jacket must fall back to same song base");
        check(catalog.lookup(7,0,"")->difficulty=="NOVICE  Lv. 5.0","low levels must use catalog-wide scale");
        const auto image=training::load_jacket(selected);
        check(image.width==2 && image.height==1 && image.bgra.size()==8,"PNG dimensions mismatch");
        check(image.bgra[0]==20 && image.bgra[1]==50 && image.bgra[2]==200 && image.bgra[3]==255,"BGRA channels mismatch");
        check(catalog.lookup(99,4,"")->title.empty(),"unknown music must not reuse previous metadata");
        check(catalog.lookup(99,4,"/data/music/../../outside/file.vox")->jacket.empty(),"chart must not escape music directory");
        write(selected,"not a PNG"); check(training::load_jacket(selected).bgra.empty(),"invalid PNG must be rejected");
        check(training::load_jacket(root/L"missing.png").bgra.empty(),"missing PNG must be tolerated");
        write(database,"<mdb><music>"); check(!catalog.load(root,error),"malformed XML must fail");
        check(catalog.size()==0 && catalog.lookup(7,3,"")->title.empty(),"failed reload must clear previous catalog");
        write(database,"<?xml version=\"1.0\"?><!DOCTYPE mdb [<!ENTITY x SYSTEM 'file:///secret'>]><mdb><music id=\"7\">&x;</music></mdb>");
        check(!catalog.load(root,error),"external entities/DTD must be rejected");
        write(database,"<mdb><music id=\"7\"><info><title_name>Legacy</title_name></info><difficulty><exhaust><difnum>18</difnum></exhaust></difficulty></music></mdb>");
        check(catalog.load(root,error) && catalog.lookup(7,2,"")->difficulty=="EXHAUST  Lv. 18.0","integer-level catalogs must retain their level");
        check(training::difficulty_name(5)=="ULTIMATE","sixth difficulty must be named");
        check(training::difficulty_name(20)=="CHART 20","unknown difficulty must retain its numeric identity");
        std::cout<<"Song metadata, encoding, jacket and fallback scenarios passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
