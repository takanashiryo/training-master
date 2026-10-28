#include "ui/i18n.hpp"
#include <imgui_internal.h>
#include <iostream>
#include <regex>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
std::vector<std::string> formats(const char* text) {
    static const std::regex pattern(R"(%[-+ #0]*(?:[0-9]+|\*)?(?:\.(?:[0-9]+|\*))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn%])");
    const std::string value(text); std::vector<std::string> result;
    for(auto it=std::sregex_iterator(value.begin(),value.end(),pattern);it!=std::sregex_iterator();++it) result.push_back(it->str());
    return result;
}
}
int main() {
    using namespace training::i18n;
    try {
        check(current_language==Language::English,"English must be the default language");
        std::set<std::string> keys;
        for(const auto& entry:catalog) {
            check(keys.insert(entry.english).second,"translation keys must be unique");
            check(*entry.chinese!=0,"Chinese translations must be nonempty");
            check(formats(entry.english)==formats(entry.chinese),"format arguments must match across languages");
            set_language(Language::English);
            check(std::string_view(text(entry.english))==entry.english,"English text must retain its original wording");
            check(std::string_view(label(entry.english))==entry.english_label,"English labels must use the stable catalog ID");
            set_language(Language::Chinese);
            check(std::string_view(text(entry.english))==entry.chinese,"Chinese text must use the catalog");
            check(ImHashStr(entry.english_label,0,123)==ImHashStr(label(entry.english),0,123),"translated widget IDs must match English");
        }
        check(std::string_view(text("Unrecognized native diagnostic"))=="Unrecognized native diagnostic","unknown diagnostics must remain available");
        check(message("Cannot save trainingmaster.ini (Windows error 5)")=="无法保存 trainingmaster.ini（Windows 错误 5）","dynamic save errors must retain their Windows code");
        std::cout<<keys.size()<<" translation entries: format and stable ID checks passed\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
