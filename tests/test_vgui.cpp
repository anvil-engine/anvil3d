#include "vgui/resources.h"
#include "vgui/scheme.h"
#include "vgui/font.h"
#include "vgui/panel.h"
#include "filesystem/filesystem.h"
#include "filesystem/gameinfo.h"
#include "check.h"
#include <algorithm>
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
  auto panelTree=parseKeyValues(R"("Resource/Test.res" {
    Root { ControlName Frame fieldName Root xpos 0 ypos 0 wide f0 tall 480 visible 1 title "#Label" }
    Center { ControlName Button xpos c-50 ypos 20 wide 100 tall 24 tabPosition 2 labelText "#Other" Command Go Default 1 }
    Right { ControlName Label xpos r20 ypos 50 wide 10 tall 10 enabled 0 }
  })");
  CHECK(panelTree);
  if(panelTree) {
    auto panels=vgui::panelResources(*panelTree,localization,&error);CHECK(panels&&panels->size()==3);
    if(panels&&panels->size()==3) {
      CHECK((*panels)[0].fieldName=="Root"&&(*panels)[0].title=="Пункт");
      CHECK((*panels)[1].label=="Other"&&(*panels)[1].command=="Go"&&(*panels)[1].defaultButton);
      auto rect=vgui::resolvePanelRect((*panels)[1],640,480,&error);
      CHECK(rect&&rect->x==270&&rect->y==20&&rect->wide==100&&rect->tall==24);
      rect=vgui::resolvePanelRect((*panels)[2],640,480,&error);
      CHECK(rect&&rect->x==620&&!(*panels)[2].enabled);
      rect=vgui::resolvePanelRect((*panels)[0],640,480,&error);
      CHECK(rect&&rect->wide==640);
      auto runtime=vgui::PanelRuntime::instantiate(std::move(*panels),640,480,&error);
      CHECK(runtime&&runtime->controls().size()==3&&runtime->unsupportedCount()==0);
      CHECK(runtime&&runtime->moveFocus()&&runtime->activateFocused()=="Go");
      CHECK(runtime&&runtime->moveFocus()&&runtime->activateFocused()=="Go");
      CHECK(runtime&&runtime->activateAt(270,20)=="Go");
      CHECK(runtime&&!runtime->activateAt(620,50));
    }
  }
  for(const char* body:{R"(Child { xpos "c" })",R"(Child { wide "r10" })",
                        R"(Child { xpos "f10" })",R"(Child { visible "yes" })",
                        R"(Child { tabPosition "-1" })"}) {
    auto invalid=parseKeyValues(std::string("Root { ")+body+" }");
    CHECK(invalid&&!vgui::panelResources(*invalid,localization,&error));
  }
  memory->data["resource/scheme.res"]=R"(Scheme {
    Colors { Paper "10 20 30 40" Alias "Paper" Transparent "0 0 0 0" Bad "256 0 0 255" }
    BaseSettings {
      "Label.TextColor" "Alias" "Label.BgColor" "Transparent"
      "Button.TextColor" "Label.TextColor" "Button.BgColor" "Transparent"
      "Panel.BgColor" "Transparent" A "b" B "A"
    }
    Fonts { Default {
      low { name "Fixture" tall 12 yres "1 480" range "0x0000 0x007f" antialias 1 }
      high { name "Fixture" tall 18 yres "481 2000" range "0x0000 0x007f" }
      fallback { name "Other Face" tall 13 range "0x0000 0x04ff" }
    } Direct {name "Direct Face" tall 16} }
    CustomFontFiles { file "resource/face.ttf" file "resource\\other.ttf" }
    Borders {
      ButtonBorder RaisedBorder
      ButtonKeyFocusBorder RaisedBorder
      RaisedBorder {
        Left { 1 { color Paper offset "0 1" } }
        Top { 1 { color Alias offset "1 0" } }
        Right { 1 { color Transparent offset "0 0" } }
        Bottom { 1 { color Paper offset "0 0" } }
      }
      A B
      B A
    }
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
  auto border=scheme.border("ButtonBorder",&error);
  CHECK(border&&border->lines.size()==4);
  if(border) {
    CHECK(border->lines[0].side==vgui::BorderSide::Left&&border->lines[0].offsetY==1);
    CHECK((border->lines[1].color==vgui::Color{10,20,30,40}));
  }
  CHECK(!scheme.border("A",&error)&&error.find("cycle")!=std::string::npos);
  CHECK(!scheme.border("Missing",&error)&&error.find("missing")!=std::string::npos);
  if(panelTree) {
    auto descriptors=vgui::panelResources(*panelTree,localization,&error);
    auto runtime=descriptors?vgui::PanelRuntime::instantiate(std::move(*descriptors),640,480,&error):std::nullopt;
    CHECK(runtime&&runtime->moveFocus());
    auto paint=runtime?runtime->paint(scheme,&error):std::nullopt;
    CHECK(paint&&paint->solids.size()==3&&paint->borders.size()==1&&paint->text.size()==1&&paint->unsupported==0);
    if(paint) {
      CHECK(paint->text[0].text=="Other"&&paint->text[0].font=="Default");
      CHECK(paint->text[0].alignment==vgui::TextAlignment::Center);
      CHECK(paint->borders[0].border.lines.size()==4);
      auto batch=vgui::paintBatch(*paint,10,20,{0,0,640,480},&error);
      CHECK(batch&&batch->cmds.size()==1&&batch->vertices.size()==28&&batch->indices.size()==42);
      if(batch) CHECK(batch->vertices[0].x==10&&batch->vertices[0].y==20&&batch->cmds[0].texture==0);
    }
  }
  vgui::PaintPlan huge;
  huge.solids.resize(262145);
  CHECK(!vgui::paintBatch(huge,0,0,{0,0,1,1},&error)&&error.find("262144")!=std::string::npos);
  CHECK(scheme.customFontFiles().size()==2&&scheme.customFontFiles()[1]=="resource/other.ttf");
  auto fonts=scheme.fontCandidates("default",480,'A',&error);
  const auto familyNames=scheme.fontFamilyNames();
  CHECK(std::find(familyNames.begin(),familyNames.end(),"Fixture")!=familyNames.end());
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
  vgui::FontLibrary fontLibrary;
  CHECK(fontLibrary.valid());
  auto loaded=fontLibrary.loadCustomFiles(fs,scheme);
  CHECK(loaded.declared==2&&loaded.files==0&&loaded.faces==0&&loaded.missing==2&&loaded.invalid==0);
  memory->data["resource/bad.ttf"]="not a font";
  memory->data["resource/fontscheme.res"]=R"(Scheme {Fonts {Bad {name "Bad" tall 12}} CustomFontFiles {file "resource/bad.ttf"}})";
  CHECK(scheme.load(fs,"resource/fontscheme.res",&error));
  loaded=fontLibrary.loadCustomFiles(fs,scheme);
  CHECK(loaded.declared==1&&loaded.files==0&&loaded.faces==0&&loaded.missing==0&&loaded.invalid==1);
  CHECK(!fontLibrary.rasterize(scheme,"Bad",480,'A',&error)&&error.find("no loaded original face")!=std::string::npos);
  memory->data["resource/fontscheme.res"]=R"(Scheme {Fonts {Bad {name "Bad" tall nope}}})";
  CHECK(scheme.load(fs,"resource/fontscheme.res",&error));
  CHECK(!fontLibrary.rasterize(scheme,"Bad",480,'A',&error)&&error.find("invalid tall")!=std::string::npos);
  for(const std::string& invalid:{std::string("\xc0\x80",2),std::string("\xe2\x82",2),
                                  std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\0",1)})
    CHECK(!fontLibrary.rasterizeText(scheme,"Bad",480,invalid,&error)&&error.find("UTF-8")!=std::string::npos);
  CHECK(!fontLibrary.rasterizeText(scheme,"Bad",480,std::string(4097,'A'),&error)&&error.find("4096")!=std::string::npos);
  CHECK(vgui::textTexture({1,1,0,0,{}}).pixels.empty());
  CHECK(vgui::textBatch(0,{1,1,0,0,{255}},0,0,{255,255,255,255},{0,0,10,10}).cmds.empty());
  memory->data["resource/invalidscheme.res"]=R"(Scheme {CustomFontFiles {file "../outside.ttf"}})";
  CHECK(!scheme.load(fs,"resource/invalidscheme.res",&error));
  CHECK(scheme.customFontFiles().empty()); // failed load preserves the current scheme and its views
  CHECK(scheme.fontCandidates("Bad",480,'A',&error));
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
      if(std::string_view(name)=="resource/newgamedialog.res" || std::string_view(name)=="resource/optionssubkeyboard.res") {
        auto panels=vgui::panelResources(*resource,vgui::Localization{},&error);
        if(!panels) std::fprintf(stderr,"%s: %s\n",name,error.c_str());
        CHECK(panels&&!panels->empty());
        if(panels) {
          for(const auto& panel:*panels) CHECK(vgui::resolvePanelRect(panel,1280,720,&error));
          auto runtime=vgui::PanelRuntime::instantiate(std::move(*panels),1280,720,&error);
          CHECK(runtime);
          if(runtime&&std::string_view(name)=="resource/newgamedialog.res") {
            CHECK(runtime->controls().size()==7&&runtime->unsupportedCount()==1);
            CHECK(runtime->moveFocus()&&runtime->activateFocused()=="Play");
            CHECK(runtime->moveFocus()&&runtime->activateFocused()=="Close");
          }
          if(runtime&&std::string_view(name)=="resource/optionssubkeyboard.res") {
            CHECK(runtime->controls().size()==5&&runtime->unsupportedCount()==1);
            CHECK(runtime->moveFocus()&&runtime->activateFocused()=="Defaults");
          }
        }
      }
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
        if(std::string_view(name)=="resource/sourcescheme.res") {
          auto buttonBorder=originalScheme.border("ButtonBorder",&error);
          if(!buttonBorder) std::fprintf(stderr,"%s: %s\n",name,error.c_str());
          CHECK(buttonBorder&&buttonBorder->lines.size()==4);
          auto focusBorder=originalScheme.border("ButtonKeyFocusBorder",&error);
          CHECK(focusBorder&&focusBorder->lines.size()==8);
        }
        if(std::string_view(name)=="resource/sourcescheme.res")
          CHECK(originalScheme.color("Label.TextColor",&error));
        auto candidates=originalScheme.fontCandidates("Default",768,'A',&error);
        CHECK(candidates&&!candidates->empty());
        vgui::FontLibrary originalFonts;
        const auto loadedFonts=originalFonts.loadCustomFiles(real,originalScheme);
        CHECK(originalFonts.valid());
        CHECK(loadedFonts.declared==originalScheme.customFontFiles().size());
        CHECK(loadedFonts.files+loadedFonts.missing+loadedFonts.invalid==loadedFonts.declared);
        CHECK(loadedFonts.files>0&&loadedFonts.faces>=loadedFonts.files);
        if(std::string_view(name)=="resource/sourcescheme.res") {
          const auto systemFonts=originalFonts.loadSystemFonts(originalScheme);
          CHECK(systemFonts.scanned>0&&systemFonts.files>0&&systemFonts.faces>0);
          auto glyph=originalFonts.rasterize(originalScheme,"Default",768,'A',&error);
          if(!glyph) std::fprintf(stderr,"system Default: %s\n",error.c_str());
          CHECK(glyph&&glyph->width>0&&glyph->height>0);
          auto text=originalFonts.rasterizeText(originalScheme,"Default",768,"START",&error);
          CHECK(text&&text->width>0&&text->height>0);
          if(text) {
            vgui::TextPaint placement{{10,20,100,24},"START","Default",{255,255,255,255},vgui::TextAlignment::West};
            auto batch=vgui::paintTextBatch(7,*text,placement,30,40,{0,0,640,480});
            CHECK(batch.cmds.size()==1&&batch.cmds[0].texture==7&&batch.vertices[0].x==40);
            CHECK(batch.cmds[0].clip.x==40&&batch.cmds[0].clip.y==60&&batch.cmds[0].clip.width==100);
          }
        }
        if(std::string_view(name)=="resource/clientscheme.res") {
          auto glyph=originalFonts.rasterize(originalScheme,"ClientTitleFont",768,'H',&error);
          if(!glyph) std::fprintf(stderr,"%s: %s\n",name,error.c_str());
          CHECK(glyph&&glyph->width>0&&glyph->height>0&&glyph->advance>0);
          if(glyph) {
            CHECK(glyph->coverage.size()==size_t(glyph->width)*glyph->height);
            CHECK(std::any_of(glyph->coverage.begin(),glyph->coverage.end(),[](uint8_t alpha){return alpha!=0;}));
          }
          auto title=originalFonts.rasterizeText(originalScheme,"ClientTitleFont",768,"HALF-LIFE 2",&error);
          if(!title) std::fprintf(stderr,"%s: %s\n",name,error.c_str());
          CHECK(title&&title->width>glyph->width&&title->height>0&&title->advance>0);
          if(title) {
            auto texture=vgui::textTexture(*title);
            CHECK(texture.desc.width==title->width&&texture.desc.height==title->height&&texture.pixels.size()==title->coverage.size()*4);
            auto batch=vgui::textBatch(7,*title,10,20,{1,2,3,4},{0,0,1280,720});
            CHECK(batch.vertices.size()==4&&batch.indices.size()==6&&batch.cmds.size()==1&&batch.cmds[0].texture==7);
            CHECK(batch.vertices[0].color==0x04030201&&batch.vertices[2].x==10+title->width&&batch.vertices[2].y==20+title->height);
          }
        }
        std::printf("Scheme %s: %zu/%zu original custom font files, %zu faces\n",name,
                    loadedFonts.files,loadedFonts.declared,loadedFonts.faces);
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
    auto dialog=vgui::loadResource(real,"resource/newgamedialog.res",&error);
    vgui::Scheme sourceScheme;
    CHECK(dialog&&sourceScheme.load(real,"resource/sourcescheme.res",&error));
    if(dialog) {
      auto descriptors=vgui::panelResources(*dialog,original,&error);
      auto runtime=descriptors?vgui::PanelRuntime::instantiate(std::move(*descriptors),600,296,&error):std::nullopt;
      CHECK(runtime&&runtime->moveFocus());
      auto paint=runtime?runtime->paint(sourceScheme,&error):std::nullopt;
      if(!paint) std::fprintf(stderr,"NewGameDialog paint: %s\n",error.c_str());
      CHECK(paint&&paint->unsupported==1&&paint->borders.size()==3&&paint->text.size()==3);
    }
    CHECK(original.load(real,"resource/gameui_russian.txt",&error));
    CHECK(original.resolve("#GameUI_GameMenu_Quit")!="#GameUI_GameMenu_Quit");
  }
  return TEST_RESULT();
}
