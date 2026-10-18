#include "widget_card.hpp"
#include "widget_images.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <atomic>
#include <functional>
#include <fstream>
#include <filesystem>
#include <set>
using namespace winrt::Windows::Data::Json;
static void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static std::vector<JsonObject> actions(const JsonObject& card){
    std::vector<JsonObject> result;
    std::function<void(IJsonValue)> visit=[&](IJsonValue value){
        if(value.ValueType()==JsonValueType::Object){auto item=value.GetObject();if(item.GetNamedString(L"type",L"")==L"Action.Execute")result.push_back(item);for(auto pair:item)visit(pair.Value());}
        else if(value.ValueType()==JsonValueType::Array)for(auto child:value.GetArray())visit(child);
    };visit(card);return result;
}
static std::pair<int,int> png_dimensions(const std::string& uri){
    constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::vector<unsigned char> bytes;uint32_t buffer=0;int bits=0;
    for(char ch:uri.substr(22)){auto value=alphabet.find(ch);if(value==std::string_view::npos)break;buffer=(buffer<<6)|static_cast<uint32_t>(value);bits+=6;if(bits>=8){bits-=8;bytes.push_back(static_cast<unsigned char>(buffer>>bits));if(bytes.size()==24)break;}}
    expect(bytes.size()==24,"PNG must contain an IHDR header.");auto read=[&](int offset){int value=0;for(int i=0;i<4;++i)value=(value<<8)|bytes[offset+i];return value;};return {read(16),read(20)};
}
static std::array<uint8_t,4> png_pixel(const std::string& uri,int x,int y){
    constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::vector<uint8_t> bytes;uint32_t buffer=0;int bits=0;
    for(char ch:uri.substr(22)){auto value=alphabet.find(ch);if(value==std::string_view::npos)break;buffer=(buffer<<6)|static_cast<uint32_t>(value);bits+=6;if(bits>=8){bits-=8;bytes.push_back(static_cast<uint8_t>(buffer>>bits));}}
    winrt::com_ptr<IStream> stream;winrt::check_hresult(CreateStreamOnHGlobal(nullptr,TRUE,stream.put()));winrt::check_hresult(stream->Write(bytes.data(),static_cast<ULONG>(bytes.size()),nullptr));LARGE_INTEGER start{};winrt::check_hresult(stream->Seek(start,STREAM_SEEK_SET,nullptr));
    winrt::com_ptr<IWICImagingFactory> factory;winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.put())));
    winrt::com_ptr<IWICBitmapDecoder> decoder;winrt::check_hresult(factory->CreateDecoderFromStream(stream.get(),nullptr,WICDecodeMetadataCacheOnLoad,decoder.put()));winrt::com_ptr<IWICBitmapFrameDecode> frame;winrt::check_hresult(decoder->GetFrame(0,frame.put()));
    winrt::com_ptr<IWICFormatConverter> converter;winrt::check_hresult(factory->CreateFormatConverter(converter.put()));winrt::check_hresult(converter->Initialize(frame.get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    WICRect area{x,y,1,1};std::array<uint8_t,4> pixel{};winrt::check_hresult(converter->CopyPixels(&area,4,4,pixel.data()));return pixel;
}
int main(int argc,char** argv){try{
    winrt::init_apartment();
    std::string diagnostic;try{throw std::runtime_error("Earbud Controls diagnostic sentinel");}catch(const std::exception& e){diagnostic=e.what();}
    expect(diagnostic=="Earbud Controls diagnostic sentinel","Native exceptions must remain readable.");
    try{check(MDR_RESULT_ERROR_TIMEOUT,"Diagnostic timeout test");}catch(const std::exception& e){diagnostic=e.what();}
    expect(diagnostic=="Diagnostic timeout test: Timed out","Engine errors must remain readable.");
    Snapshot cached;cached.checked=1;cached.batteries={82,-1,-1};cached.noise=2;
    expect(battery_text(cached).find("0%")==std::string::npos,"Missing battery must not become zero.");
    expect(battery_text(cached).find("R --")!=std::string::npos,"Missing right battery must remain unknown.");
    Snapshot live=cached;live.ready=live.fresh=live.eq_supported=live.noise_supported=live.ambient_supported=true;live.preset=MDR_EQ_USER_2;live.bands={1,0,1,2,3};live.bass=4;
    live.batteries={90,0,50};
    for(auto id:{0,8,9,10,11,12,13,14,15,24,25,26})live.presets.emplace_back(id,preset_name(id));
    WidgetIcons icons{"data:image/png;base64,AA==","data:image/png;base64,AA=="};
    for(auto glyph:{"case","noise-cancelling","ambient","off","equalizer","back","more","previous","next","plus","minus","check","bluetooth","bass","link","diagnostics","log"}){icons.symbols[glyph]={icons.light,icons.dark};icons.symbols[std::string(glyph)+"-selected"]={icons.light,icons.dark};}
    for(int i=0;i<3;++i)icons.batteries[i]={battery_image(live.batteries[i],i==2,false,false),battery_image(live.batteries[i],i==2,true,false)};
    icons.marker={selection_marker(false,false),selection_marker(true,false)};
    icons.button_frames=button_frame_artwork(false);
        icons.border={border_image(false,false),border_image(true,false)};icons.battery_readouts=battery_readout_artwork(live.batteries,false);
    icons.surface={button_surface(false,false),button_surface(true,false)};icons.divider={divider_image(false,false),divider_image(true,false)};
    for(auto [width,height]:{std::pair{28,28},std::pair{130,44},std::pair{85,46},std::pair{85,62}}){
        auto& frame=icons.button_frames.at(height).second;
        expect(png_dimensions(frame)==std::pair{width*4,height*4},"Frames must match each control's aspect ratio at 4x resolution.");
        for(auto [x,y]:{std::pair{width*2,0},std::pair{width*2,height*4-1},std::pair{0,height*2},std::pair{width*4-1,height*2}})expect(png_pixel(frame,x,y)[3]==255,"A control must contain all four border edges in one image.");
        expect(png_pixel(frame,width*2,height*2)[3]==16,"The dark control surface must let the host hover show through.");
        expect(png_pixel(icons.button_frames.at(height).first,width*2,height*2)[3]==8,"The light control surface must let the host hover show through.");
        expect(png_pixel(frame,0,0)[3]==0,"Small rounded corners must remain transparent.");
        auto selected=png_pixel(icons.button_frames.at(height+1000).second,width*2,height*4-4);
        expect(selected==std::array<uint8_t,4>{255,194,76,255},"The selected control must retain the mock's cyan bottom indicator.");
    }
    expect(battery_low(0)&&battery_low(19)&&battery_low(20)&&!battery_low(21)&&!battery_low(-1)&&!battery_low(101),"Low battery must include exactly 20 and exclude invalid readings.");
    for(int i=0;i<2;++i)icons.switches[i]={switch_image(i!=0,false,false),switch_image(i!=0,true,false)};
    expect(battery_image(0,false,true,false)!=battery_image(90,false,true,false),"Battery fill must reflect the reported percentage.");
    expect(battery_image(-1,false,true,false)!=battery_image(0,false,true,false),"Unknown and empty batteries must look different.");
    expect(switch_image(true,true,false)!=switch_image(false,true,false),"Preference switches must reflect their state.");
    std::filesystem::path export_folder;
    if(argc==3&&std::string_view(argv[1])=="--cards-dir"){
        export_folder=argv[2];std::filesystem::create_directories(export_folder);
        for(auto& [glyph,urls]:icons.symbols){auto base=glyph;bool selected=base.ends_with("-selected");if(selected)base.resize(base.size()-9);
            auto load=[&](const char* theme){std::ifstream in(export_folder/"icons"/(base+"-"+theme+".png"),std::ios::binary);std::string data((std::istreambuf_iterator<char>(in)),{});expect(!data.empty(),"Review artwork must exist.");return png_uri({reinterpret_cast<const uint8_t*>(data.data()),data.size()});};urls={load(selected?"accent-light":"light"),load(selected?"accent":"dark")};
        }
    }
    auto curve=curve_image(live.bands,RGB(190,179,255),true,false);
    expect(curve.starts_with("data:image/png;base64,iVBORw0KGgo"),"EQ preview must be a valid locally encoded PNG.");
    icons.curve_dark=curve;icons.curve_light=curve_image(live.bands,RGB(104,86,199),false,false);icons.curves=curve_artwork(live.bands,false);
    expect(png_dimensions(icons.curves["large"].first)==std::pair{1072,352},"Large curve must match the 268 by 88 layout at 4x resolution.");
    expect(png_dimensions(icons.curves["medium"].first)==std::pair{1072,176},"Medium curve must have its own aspect ratio.");
    expect(png_dimensions(icons.curves["summary"].first)==std::pair{1072,352},"Home summary must have its own aspect ratio.");
    if(argc==3&&std::string_view(argv[1])=="--curve-preview"){std::ofstream out(argv[2]);out<<curve;}
    for(auto size:{WidgetSize::Small,WidgetSize::Medium,WidgetSize::Large}){
        for(auto page:{WidgetPage::Home,WidgetPage::Equalizer,WidgetPage::Presets,WidgetPage::More,WidgetPage::Diagnostics,WidgetPage::Noise}){
            for(bool pending:{false,true}){
                auto state=cached;state.pending=pending;WidgetView view;view.page=page;
                auto card=make_card(state,size,view,icons);auto parsed=JsonObject::Parse(winrt::to_hstring(card));
                expect(card.find("Confirmed on earbuds")==std::string::npos,"Routine success must not add a redundant caption.");
                expect(card.find("tooltip")==std::string::npos,"Routine actions must not obscure adjacent controls with explicit tooltips.");
                expect(card.size()<128*1024,"Card payload must stay bounded.");
                expect(card.find("open.sound")==std::string::npos,"Widget EQ must never launch the editor.");
                int noise=0;std::set<std::wstring> ids;
                for(auto button:actions(parsed)){
                    auto verb=std::wstring(button.GetNamedString(L"verb"));
                    expect(ids.insert(std::wstring(button.GetNamedString(L"id"))).second,"Action IDs must remain unique.");
                    if(verb.starts_with(L"noise.")){++noise;expect(!button.GetNamedBoolean(L"isEnabled"),"Stale noise controls must be disabled.");}
                    if(verb.starts_with(L"eq."))expect(!button.GetNamedBoolean(L"isEnabled"),"Stale EQ controls must be disabled.");
                    if(verb==L"retry")expect(button.GetNamedBoolean(L"isEnabled")==!pending,"Retry must not duplicate an in-flight operation.");
                }
                expect(noise==(page==WidgetPage::Home||(size==WidgetSize::Small&&(page==WidgetPage::Noise||page==WidgetPage::Equalizer||page==WidgetPage::Presets))?3:0),"Home must expose all three direct noise controls, including Small.");
                if(page==WidgetPage::Home)expect(card.find("Last seen")!=std::string::npos,"Cached readings must be explicitly labelled.");
            }
            WidgetView view;view.page=page;view.band=1;
            auto card=make_card(live,size,view,icons);auto parsed=JsonObject::Parse(winrt::to_hstring(card));
            std::set<std::wstring> live_ids;for(auto button:actions(parsed))expect(live_ids.insert(std::wstring(button.GetNamedString(L"id"))).second,"Live action IDs must remain unique during state refreshes.");
            expect(card.find("data:image/png;base64,")!=std::string::npos,"Cards must use local artwork.");
            if(page==WidgetPage::Equalizer){if(size!=WidgetSize::Small)expect(card.find("1 kHz")!=std::string::npos,"EQ must show the selected band.");int edits=0;for(auto button:actions(parsed))if(std::wstring_view(button.GetNamedString(L"verb")).starts_with(L"eq.increase."))++edits;expect(edits==(size==WidgetSize::Small?0:size==WidgetSize::Large?6:2),"Large EQ must expose all bands; Medium exposes the selected band and Clear Bass.");}
            if(page==WidgetPage::Presets){int presets=0;for(auto button:actions(parsed))if(std::wstring_view(button.GetNamedString(L"verb")).starts_with(L"eq.preset."))++presets;expect(presets==(size==WidgetSize::Small?0:size==WidgetSize::Large?12:6),"Preset pagination must fit each size.");}
            if(!export_folder.empty()){
                auto size_name=size==WidgetSize::Small?"small":size==WidgetSize::Large?"large":"medium";
                const char* page_names[]={"home","eq","presets","more","device","noise"};
                for(int variant=0;variant<3;++variant){auto state=live;state.noise=variant==0?2:1;state.ambient=14;if(variant==2){state.ready=state.fresh=false;state.error="Transport unavailable";}
                    std::ofstream out(export_folder/(std::string(size_name)+"-"+page_names[static_cast<int>(page)]+"-"+std::to_string(variant)+".json"));out<<make_card(state,size,view,icons);
                }
            }
        }
    }
    for(auto verb:{L"view.eq",L"view.presets",L"view.band.1",L"eq.increase.0",L"eq.preset.26"})expect(!widget_eq_action_allowed(WidgetSize::Small,verb),"Small must reject stale or forged EQ events.");
    expect(widget_eq_action_allowed(WidgetSize::Medium,L"eq.increase.0")&&widget_eq_action_allowed(WidgetSize::Small,L"noise.anc"),"Size guard must retain medium EQ and one-click Small sound changes.");
    auto threshold=live;threshold.batteries={20,21,-1};auto threshold_icons=icons;threshold_icons.battery_readouts=battery_readout_artwork(threshold.batteries,false);for(int i=0;i<3;++i)threshold_icons.batteries[i]={battery_image(threshold.batteries[i],i==2,false,false),battery_image(threshold.batteries[i],i==2,true,false)};auto threshold_card=make_card(threshold,WidgetSize::Medium,{},threshold_icons);auto threshold_json=JsonObject::Parse(winrt::to_hstring(threshold_card));
    if(!export_folder.empty()){std::ofstream out(export_folder/"medium-home-3.json");out<<threshold_card;}
    std::map<std::wstring,std::wstring> readout_colors;std::function<void(IJsonValue)> collect=[&](IJsonValue value){if(value.ValueType()==JsonValueType::Object){auto item=value.GetObject();if(item.GetNamedString(L"type",L"")==L"TextBlock")readout_colors[std::wstring(item.GetNamedString(L"text",L""))]=std::wstring(item.GetNamedString(L"color",L""));for(auto pair:item)collect(pair.Value());}else if(value.ValueType()==JsonValueType::Array)for(auto child:value.GetArray())collect(child);};collect(threshold_json);
    expect(threshold_card.find("Left battery 20%, low")!=std::string::npos&&readout_colors[L"21%"]==L"Default"&&readout_colors[L"--"]==L"Default"&&threshold_card.find("Attention")==std::string::npos,"Low readings must use the approved muted-red artwork and keep accessible text.");
    for(auto size:{WidgetSize::Small,WidgetSize::Medium,WidgetSize::Large}){auto home=make_card(live,size,{},icons);auto actions_list=actions(JsonObject::Parse(winrt::to_hstring(home)));int mode_count=0;for(auto item:actions_list){auto verb=item.GetNamedString(L"verb");if(std::wstring_view(verb).starts_with(L"noise."))++mode_count;if(size==WidgetSize::Small)expect(!std::wstring_view(verb).starts_with(L"eq.")&&verb!=L"view.eq"&&verb!=L"view.presets","Small must expose no EQ controls.");expect(verb!=L"view.more","Preferences must use the host customization menu.");}expect(mode_count==3,"A sound mode must be selectable directly in every size.");}
    WidgetView first,second;expect(navigate_widget(first,L"view.eq"),"Handle EQ navigation.");navigate_widget(first,L"view.band.next");
    expect(second.page==WidgetPage::Home&&second.band==0,"View state must be isolated per widget.");
    expect(navigate_widget(second,L"view.band.4")&&second.band==4&&!navigate_widget(second,L"view.band.9"),"Direct band selection must remain bounded.");
    auto change=eq_command(first,live,L"eq.increase.0");expect(change&&change->bands==std::vector<int32_t>({2,0,1,2,3}),"A band edit must preserve the other bands.");
    expect(live.bands==std::vector<int32_t>({1,0,1,2,3}),"Pending changes must not overwrite confirmed state.");
    first.baseline=EqBaseline{live.preset,live.bass,live.bands};first.proposed=*change;
    auto pending=live;pending.pending=true;expect(make_card(pending,WidgetSize::Medium,first,icons).find("→")==std::string::npos,"Only the edited band should show a pending value.");
    first.band=0;expect(make_card(pending,WidgetSize::Medium,first,icons).find("→ +2")!=std::string::npos,"Pending values must be visible before readback.");
    auto failed=live;failed.pending=false;failed.ready=failed.fresh=false;failed.error="Transport: destination host was down";reconcile_widget(first,failed);
    expect(!first.proposed&&first.failed_write,"Failed writes must clear the optimistic value.");
    expect(make_card(failed,WidgetSize::Medium).find(failed.error)==std::string::npos,"Home must not expose technical transport errors.");
    auto reset=eq_command(first,live,L"eq.reset");expect(reset&&reset->bands==live.bands&&reset->bass==4&&reset->preset==26,"Reset must restore the captured editing baseline.");
    expect(!eq_command(first,live,L"eq.preset.999"),"Only advertised presets may be selected.");
    expect(!eq_command(first,live,L"eq.increase.99"),"Invalid band indices must not become writes.");
    auto factory=live;factory.preset=MDR_EQ_BRIGHT;expect(!eq_command(first,factory,L"eq.increase.0"),"Factory presets must not be edited.");
    auto upper=live;upper.bands[0]=10;upper.bass=10;expect(!eq_command(first,upper,L"eq.increase.0")&&!eq_command(first,upper,L"eq.increase.5"),"Band and bass upper limits must be enforced.");
    auto lower=live;lower.bands[0]=-10;lower.bass=-10;expect(!eq_command(first,lower,L"eq.decrease.0")&&!eq_command(first,lower,L"eq.decrease.5"),"Band and bass lower limits must be enforced.");
    expect(!eq_command(first,pending,L"eq.increase.0")&&!eq_command(first,failed,L"eq.increase.0"),"Unresolved or stale settings must not produce new writes.");
    navigate_widget(first,L"view.home");navigate_widget(first,L"view.eq");expect(first.band==0&&first.baseline.has_value(),"Navigation must preserve the edit baseline.");
    reconcile_widget(first,live);expect(!first.failed_write,"A successful refresh must clear a failed write.");
    auto other_preset=live;other_preset.preset=MDR_EQ_USER_1;reconcile_widget(first,other_preset);expect(!first.baseline,"Changing presets must invalidate the previous preset baseline.");
    std::atomic<int> notifications{};{Controller controller([&]{++notifications;},live);expect(!controller.send({Operation::Noise,1})&&!controller.send({Operation::Retry}),"Inactive controls must reject hardware actions.");}
    expect(notifications==0,"An unused controller must produce no background updates.");
    std::cout<<"Passed: widget views and sizes, icon tiles, preset pagination, bounded EQ edits, reset, failed-write rollback, local PNG curve, inactive lifecycle.\n";return 0;
}catch(const winrt::hresult_error& error){std::cerr<<winrt::to_string(error.message())<<"\n";return 1;}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
