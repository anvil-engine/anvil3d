#include "vgui/resources.h"
#include "vgui/scheme.h"
#include "filesystem/filesystem.h"
#include "filesystem/gameinfo.h"
#include "check.h"
#include <map>

using namespace anvil;
namespace {
class MemoryArchive final : public Archive {
public:
  std::map<std::string,std::string> data;
  bool contains(std::string_view path) const override { return data.contains(std::string(path)); }
  std::optional<std::string> read(std::string_view path) const override {
    const auto found=data.find(std::string(path));
    return found==data.end()?std::nullopt:std::optional(found->second);
  }
  std::vector<std::string> files() const override {
    std::vector<std::string> result; for(const auto& [path,value]:data) result.push_back(path); return result;
  }
};
std::string utf16(std::u16string_view text,bool big=false) {
  std::string out=big?"\xfe\xff":"\xff\xfe";
  for(char16_t c:text) { out+=char(big?c>>8:c&255);out+=char(big?c&255:c>>8); }
  return out;
}
}
int main(int argc,char** argv) {
  std::string error;
  CHECK(vgui::resourceText(utf16(u"A\u042f\U0001f680"))=="AЯ🚀");
  CHECK(vgui::resourceText(utf16(u"A\u042f\U0001f680",true))=="AЯ🚀");
  CHECK(!vgui::resourceText(std::string("\xff\xfe\x00",3),&error));
  CHECK(!vgui::resourceText(utf16(std::u16string(1,char16_t(0xd800))),&error));
  CHECK(!vgui::resourceText(utf16(std::u16string(1,char16_t(0xdc00))),&error));
  CHECK(!vgui::resourceText(std::string("a\0b",3),&error));
  auto escaped=parseKeyValues(R"(key "say \"hello\"\nnext\tpath\\file")",&error,true);
  CHECK(escaped&&escaped->get("key")=="say \"hello\"\nnext\tpath\\file");
  auto literal=parseKeyValues(R"(key "path\new\text")");
  CHECK(literal&&literal->get("key")==R"(path\new\text)");

  auto archive=std::make_unique<MemoryArchive>(); auto* memory=archive.get();
  memory->data["resource/child.res"]=R"(#base "base.res" #include "peers.res" Scheme { Colors { FgColor "9 8 7 255" } Fonts {} Empty "" })";
  memory->data["resource/base.res"]=R"(Scheme { Colors { FgColor "1 2 3 255" BgColor "0 0 0 255" } Fonts { Default { name "Fixture" } } Empty { value "base" } })";
  memory->data["resource/peers.res"]=R"(extra "first" extra "second")";
  memory->data["resource/lang.txt"]=utf16(u"lang { Tokens { Label \"Пункт\" } }");
  memory->data["resource/fallback.txt"]=R"(lang { Tokens { Label "Item" Other "Other" } })";
  memory->data["resource/cycle.res"]=R"(#base "cycle.res")";
  memory->data["resource/escape.res"]=R"(#include "../../outside.res")";
  memory->data["resource/absolute.res"]=R"(#include "/outside.res")";
  memory->data["resource/missing.res"]=R"(#base "absent.res")";
  FileSystem fs;fs.addArchive(std::move(archive),"memory",{"GAME"});
  // Shared schemes can live under PLATFORM while the referring resource lives under GAME.
  auto platform=std::make_unique<MemoryArchive>();
  platform->data["resource/base.res"]=memory->data["resource/base.res"];
  memory->data.erase("resource/base.res");
  fs.addArchive(std::move(platform),"platform-memory",{"PLATFORM"});
  auto tree=vgui::loadResource(fs,"resource/child.res",&error);
  CHECK(tree&&tree->children.size()==3);
  if(tree) {
    const auto* scheme=tree->find("Scheme");CHECK(scheme);
    if(scheme) {
      CHECK(scheme->find("Colors")->get("FgColor")=="9 8 7 255");
      CHECK(scheme->find("Colors")->get("BgColor")=="0 0 0 255");
      CHECK(scheme->find("Fonts")->find("Default"));
      CHECK(scheme->find("Empty")->children.empty());
    }
    CHECK(tree->children[1].value=="first"&&tree->children[2].value=="second");
  }
  CHECK(!vgui::loadResource(fs,"resource/cycle.res",&error)&&error.find("cycle")!=std::string::npos);
  CHECK(!vgui::loadResource(fs,"resource/escape.res",&error));
  CHECK(!vgui::loadResource(fs,"resource/absolute.res",&error));
  CHECK(!vgui::loadResource(fs,"resource/missing.res",&error));
  for(int i=0;i<34;++i) memory->data["resource/depth"+std::to_string(i)+".res"]="#base \"depth"+std::to_string(i+1)+".res\"";
  CHECK(!vgui::loadResource(fs,"resource/depth0.res",&error)&&error.find("depth exceeds")!=std::string::npos);
  vgui::Localization localization;
  CHECK(localization.load(fs,"resource/fallback.txt",&error));
  CHECK(localization.load(fs,"resource/lang.txt",&error));
  CHECK(localization.resolve("#LABEL")=="Пункт");CHECK(localization.resolve("#Other")=="Other");
  CHECK(!localization.load(fs,"resource/missing.res",&error));CHECK(localization.resolve("#LABEL")=="Пункт");
  CHECK(localization.resolve("#missing")=="#missing");
  auto menu=parseKeyValues(R"(GameMenu {
    resume {label "#Label" command "ResumeGame" OnlyInGame 1 InGameOrder 20}
    new {label "#Other" command "OpenNewGameDialog" NotInGame 1}
    quit {label "Exit" command "Quit" InGameOrder 10}
    vr {label "VR" command "engine vr_activate" OnlyWhenVREnabled 1}
  })");
  CHECK(menu);
  if(menu) {
    auto entries=vgui::menuItems(*menu,localization,{});CHECK(entries.size()==2&&entries[0].id=="new");
    entries=vgui::menuItems(*menu,localization,{true});CHECK(entries.size()==2&&entries[0].id=="quit"&&entries[1].label=="Пункт");
  }
  memory->data["resource/scheme.res"]=R"(Scheme {
    Colors { Paper "10 20 30 40" Alias "Paper" Transparent "0 0 0 0" Bad "256 0 0 255" }
    BaseSettings { "Label.TextColor" "Alias" "Button.TextColor" "Label.TextColor" A "b" B "A" }
    Fonts { Default {
      low { name "Fixture" tall 12 yres "1 480" range "0x0000 0x007f" antialias 1 }
      high { name "Fixture" tall 18 yres "481 2000" range "0x0000 0x007f" }
      fallback { name "Other Face" tall 13 range "0x0000 0x04ff" }
    } Direct {name "Direct Face" tall 16} }
    CustomFontFiles { file "resource/face.ttf" file "resource\\other.ttf" }
  })";
  vgui::Scheme scheme;
  CHECK(scheme.load(fs,"resource/scheme.res",&error));
  CHECK(scheme.setting("label.textcolor")=="Alias");
  CHECK((scheme.color("Button.TextColor")==vgui::Color{10,20,30,40}));
  CHECK((scheme.color("Transparent")==vgui::Color{0,0,0,0}));
  CHECK((scheme.color(" 1 2 3 ")==vgui::Color{1,2,3,255}));
  CHECK(!scheme.color("Bad",&error));
  CHECK(!scheme.color("-1 2 3",&error));
  CHECK(!scheme.color("1 2 3 4 5",&error));
  CHECK(!scheme.color("missing",&error));
  CHECK(!scheme.color("A",&error)&&error.find("cycle")!=std::string::npos);
  CHECK(scheme.customFontFiles().size()==2&&scheme.customFontFiles()[1]=="resource/other.ttf");
  auto fonts=scheme.fontCandidates("default",480,'A',&error);
  CHECK(fonts&&fonts->size()==2);
  if(fonts&&fonts->size()==2) {
    CHECK((*fonts)[0]->key=="low"&&(*fonts)[0]->get("tall")=="12");
    CHECK((*fonts)[0]->get("antialias")=="1"&&(*fonts)[1]->key=="fallback");
  }
  fonts=scheme.fontCandidates("Default",481,'A',&error);
  CHECK(fonts&&fonts->size()==2&&(*fonts)[0]->key=="high");
  fonts=scheme.fontCandidates("Default",481,0x042f,&error);
  CHECK(fonts&&fonts->size()==1&&(*fonts)[0]->key=="fallback");
  fonts=scheme.fontCandidates("Default",481,0x1f680,&error);CHECK(fonts&&fonts->empty());
  fonts=scheme.fontCandidates("Direct",480,'A',&error);
  CHECK(fonts&&fonts->size()==1&&(*fonts)[0]->get("name")=="Direct Face");
  CHECK(!scheme.fontCandidates("absent",480,'A',&error));
  CHECK(!scheme.fontCandidates("Default",0,'A',&error));
  CHECK(!scheme.fontCandidates("Default",480,0xd800,&error));
  CHECK(!scheme.fontCandidates("Default",480,0x110000,&error));
  memory->data["resource/invalidscheme.res"]=R"(Scheme {CustomFontFiles {file "../outside.ttf"}})";
  CHECK(!scheme.load(fs,"resource/invalidscheme.res",&error));
  CHECK(scheme.customFontFiles().size()==2); // failed load preserves the current scheme and its views
  CHECK((scheme.color("Paper")==vgui::Color{10,20,30,40}));
  memory->data["resource/invalidscheme.res"]=R"(Scheme {Fonts "invalid"})";
  CHECK(!scheme.load(fs,"resource/invalidscheme.res",&error));
  for(const char* interval:{"9 1","1","1 2 3","0 4294967296","0x 5","-1 5","1a 5"}) {
    memory->data["resource/invalidscheme.res"]=std::string("Scheme { Fonts { Invalid { 1 { name Face range \"")+interval+"\" } } } }";
    CHECK(scheme.load(fs,"resource/invalidscheme.res",&error));
    CHECK(!scheme.fontCandidates("Invalid",480,'A',&error)&&error.find("range")!=std::string::npos);
  }
  if(argc>1) {
    FileSystem real;const std::filesystem::path root(argv[1]);
    auto data=readOsFile(root/"hl2/gameinfo.txt");CHECK(data);if(!data)return TEST_RESULT();
    auto info=parseGameInfo(*data,root,root/"hl2");CHECK(info);if(!info)return TEST_RESULT();
    mountGameInfo(real,*info);
    for(const char* name:{"resource/gamemenu.res","resource/sourcescheme.res","resource/clientscheme.res","resource/newgamedialog.res","resource/optionssubkeyboard.res"}) {
      auto resource=vgui::loadResource(real,name,&error);
      if(!resource) std::fprintf(stderr,"%s: %s\n",name,error.c_str());
      CHECK(resource);if(!resource)continue;
      CHECK(!resource->children.empty());
      if(std::string_view(name)=="resource/sourcescheme.res" || std::string_view(name)=="resource/clientscheme.res") {
        vgui::Scheme originalScheme;
        CHECK(originalScheme.load(real,name,&error));
        const auto* rootScheme=resource->find("Scheme");CHECK(rootScheme);
        if(rootScheme) {
          if(const auto* colors=rootScheme->find("Colors")) for(const auto& color:colors->children) {
            const auto resolved=originalScheme.color(color.key,&error);
            if(!resolved) std::fprintf(stderr,"%s: %s\n",name,error.c_str());
            CHECK(resolved);
          }
          const auto* fontList=rootScheme->find("Fonts");CHECK(fontList);
          if(fontList) for(const auto& font:fontList->children) for(int height:{480,600,768,1080,2160}) {
            const auto candidates=originalScheme.fontCandidates(font.key,height,'A',&error);
            if(!candidates) std::fprintf(stderr,"%s: %s\n",name,error.c_str());
            CHECK(candidates);
          }
        }
        CHECK(originalScheme.color("Panel.FgColor",&error));
        if(std::string_view(name)=="resource/sourcescheme.res")
          CHECK(originalScheme.color("Label.TextColor",&error));
        auto candidates=originalScheme.fontCandidates("Default",768,'A',&error);
        CHECK(candidates&&!candidates->empty());
        std::printf("Scheme %s: %zu custom font files\n",name,originalScheme.customFontFiles().size());
      }
    }
    vgui::Localization original;
    CHECK(original.load(real,"resource/gameui_english.txt",&error));

    std::printf("English localization: %zu tokens\n",original.size());
    CHECK(original.size()>0);
    auto resource=vgui::loadResource(real,"resource/gamemenu.res",&error);
    CHECK(resource);
    if(resource) {
      auto entries=vgui::menuItems(*resource,original,{});CHECK(entries.size()>3);
      for(const auto& entry:entries) {CHECK(!entry.label.starts_with('#'));std::printf("%s -> %s\n",entry.label.c_str(),entry.command.c_str());}
    }
    CHECK(original.load(real,"resource/gameui_russian.txt",&error));
    CHECK(original.resolve("#GameUI_GameMenu_Quit")!="#GameUI_GameMenu_Quit");
  }
  return TEST_RESULT();
}
