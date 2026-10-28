#include "core/song_info.hpp"
#include <windows.h>
#include <msxml6.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <algorithm>
#include <charconv>
#include <cstdio>

namespace training {
namespace {
using Microsoft::WRL::ComPtr;
struct ComApartment {
    HRESULT result{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    bool valid() const { return SUCCEEDED(result) || result==RPC_E_CHANGED_MODE; }
};
struct Bstr {
    BSTR value{};
    explicit Bstr(const wchar_t* text):value(SysAllocString(text)){}
    ~Bstr() { SysFreeString(value); }
    Bstr(const Bstr&)=delete;
};
std::string utf8(BSTR text) {
    if (!text) return {};
    const auto length=static_cast<int>(SysStringLen(text));
    const int bytes=WideCharToMultiByte(CP_UTF8,0,text,length,nullptr,0,nullptr,nullptr);
    std::string out(bytes,'\0');
    if (bytes) WideCharToMultiByte(CP_UTF8,0,text,length,out.data(),bytes,nullptr,nullptr);
    return out;
}
std::string value(IXMLDOMNode* node,const wchar_t* xpath) {
    ComPtr<IXMLDOMNode> child;
    Bstr query(xpath);
    if (FAILED(node->selectSingleNode(query.value,&child)) || !child) return {};
    BSTR text{};
    if (FAILED(child->get_text(&text))) return {};
    auto out=utf8(text); SysFreeString(text); return out;
}
unsigned number(const std::string& text) {
    unsigned out{};
    const auto [end,error]=std::from_chars(text.data(),text.data()+text.size(),out);
    return error==std::errc{} && end==text.data()+text.size()?out:0;
}
bool file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path,error);
}
bool safe_folder(const std::filesystem::path& path) {
    return !path.empty() && !path.has_root_path() && path==path.filename() && path!="." && path!="..";
}
std::filesystem::path from_utf8(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}
}
std::string difficulty_name(std::uint32_t difficulty,unsigned version) {
    switch (difficulty) {
    case 0: return "NOVICE";
    case 1: return "ADVANCED";
    case 2: return "EXHAUST";
    case 3:
        switch (version) {
        case 0: case 2: return "INFINITE";
        case 3: return "GRAVITY";
        case 4: return "HEAVENLY";
        case 5: return "VIVID";
        case 6: return "EXCEED";
        default: return "INFINITE (ver. "+std::to_string(version)+")";
        }
    case 4: return "MAXIMUM";
    case 5: return "ULTIMATE";
    default: return "CHART "+std::to_string(difficulty);
    }
}
bool SongCatalog::load(const std::filesystem::path& contents,std::string& error) {
    entries_.clear(); root_=contents; tenths_=false; error.clear();
    try {
        const auto path=root_/L"data/others/music_db.xml";
        std::error_code ec;
        const auto bytes=std::filesystem::file_size(path,ec);
        if (ec || bytes>64*1024*1024) { error="music_db.xml missing or too large"; return false; }
        ComApartment apartment;
        ComPtr<IXMLDOMDocument2> doc;
        if (!apartment.valid() || FAILED(CoCreateInstance(__uuidof(DOMDocument60),nullptr,CLSCTX_INPROC_SERVER,
                                              IID_PPV_ARGS(&doc)))) { error="MSXML6 unavailable"; return false; }
        doc->put_async(VARIANT_FALSE); doc->put_validateOnParse(VARIANT_FALSE);
        doc->put_resolveExternals(VARIANT_FALSE);
        VARIANT prohibit{}; prohibit.vt=VT_BOOL; prohibit.boolVal=VARIANT_TRUE;
        Bstr property(L"ProhibitDTD"); doc->setProperty(property.value,prohibit);
        VARIANT source{}; source.vt=VT_BSTR; source.bstrVal=SysAllocString(path.c_str());
        VARIANT_BOOL ok{}; const auto hr=doc->load(source,&ok); VariantClear(&source);
        if (FAILED(hr) || !ok) { error="music_db.xml could not be parsed"; return false; }
        ComPtr<IXMLDOMNodeList> songs; Bstr query(L"/mdb/music");
        if (FAILED(doc->selectNodes(query.value,&songs)) || !songs) { error="music_db.xml has no song list"; return false; }
        long count{}; songs->get_length(&count);
        if (count>100000) { error="music_db.xml has too many songs"; return false; }
        constexpr const wchar_t* levels[]{L"difficulty/novice/difnum",L"difficulty/advanced/difnum",
            L"difficulty/exhaust/difnum",L"difficulty/infinite/difnum",L"difficulty/maximum/difnum",L"difficulty/ultimate/difnum"};
        for (long i=0;i<count;++i) {
            ComPtr<IXMLDOMNode> song; if (FAILED(songs->get_item(i,&song)) || !song) continue;
            const auto id=number(value(song.Get(),L"@id")); if (!id) continue;
            Entry entry;
            entry.title=value(song.Get(),L"info/title_name"); entry.artist=value(song.Get(),L"info/artist_name");
            entry.folder=value(song.Get(),L"info/ascii"); entry.infinite_version=number(value(song.Get(),L"info/inf_ver"));
            for (unsigned j=0;j<entry.levels.size();++j) {
                entry.levels[j]=number(value(song.Get(),levels[j]));
                tenths_|=entry.levels[j]>20;
            }
            entries_.insert_or_assign(id,std::move(entry));
        }
        if (entries_.empty()) { error="music_db.xml contains no recognized songs"; return false; }
        return true;
    } catch (const std::exception&) {
        entries_.clear(); error="Song catalog resource read failed"; return false;
    }
}
std::shared_ptr<const SongInfo> SongCatalog::lookup(std::uint32_t id,std::uint32_t difficulty,const std::string& chart) const {
    auto out=std::make_shared<SongInfo>();
    const auto found=entries_.find(id);
    const Entry* entry=found==entries_.end()?nullptr:&found->second;
    out->difficulty=difficulty_name(difficulty,entry?entry->infinite_version:0);
    if (entry) {
        out->title=entry->title; out->artist=entry->artist;
        if (difficulty<entry->levels.size()) {
            const unsigned raw=entry->levels[difficulty];
            if (raw) out->difficulty+="  Lv. "+std::to_string(raw/(tenths_?10:1))+"."+std::to_string(tenths_?raw%10:0);
        }
    }
    if (root_.empty() || difficulty>=6) return out;
    // Only use a single folder component underneath data/music, never arbitrary chart paths.
    std::filesystem::path folder;
    const std::string prefix="/data/music/";
    if (chart.starts_with(prefix)) {
        const auto part=from_utf8(chart.substr(prefix.size())).parent_path();
        if (safe_folder(part)) folder=part;
    }
    char id_text[24]{}; sprintf_s(id_text,"%04u",id);
    if (folder.empty() && entry && safe_folder(from_utf8(entry->folder)))
        folder=from_utf8(std::string(id_text)+"_"+entry->folder);
    if (folder.empty()) return out;
    const auto directory=root_/L"data/music"/folder;
    // Match the selected chart first. A song's base jacket is the final fallback.
    for (const unsigned tier:{difficulty+1,1u}) {
        for (const char* suffix:{".png","_b.png","_s.png"}) {
            const auto candidate=directory/(std::string("jk_")+id_text+"_"+std::to_string(tier)+suffix);
            if (file(candidate)) { out->jacket=candidate; return out; }
        }
    }
    return out;
}
JacketPixels load_jacket(const std::filesystem::path& path) {
    try {
        std::error_code error;
        if (path.empty() || std::filesystem::file_size(path,error)>16*1024*1024 || error) return {};
        ComApartment apartment; ComPtr<IWICImagingFactory> factory;
        if (!apartment.valid() || FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                                                          IID_PPV_ARGS(&factory)))) return {};
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder))) return {};
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0,&frame))) return {};
        UINT width{},height{};
        if (FAILED(frame->GetSize(&width,&height)) || !width || !height || width>4096 || height>4096) return {};
        ComPtr<IWICBitmapSource> source=frame;
        if (std::max(width,height)>256) {
            const float scale=256.f/std::max(width,height);
            width=std::max(1u,static_cast<unsigned>(width*scale)); height=std::max(1u,static_cast<unsigned>(height*scale));
            ComPtr<IWICBitmapScaler> scaler;
            if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
                FAILED(scaler->Initialize(frame.Get(),width,height,WICBitmapInterpolationModeFant))) return {};
            source=scaler;
        }
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter)) || FAILED(converter->Initialize(source.Get(),
            GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return {};
        JacketPixels out{width,height,std::vector<std::uint8_t>(width*height*4)};
        if (FAILED(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(out.bgra.size()),out.bgra.data()))) return {};
        return out;
    } catch (const std::exception&) { return {}; }
}
}
