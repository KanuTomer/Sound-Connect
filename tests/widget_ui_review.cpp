// Developer-only Windows-host UI review. No Controller, Bluetooth, persistence,
// shell links, or production provider identity is used by this executable.
#include <windows.h>
#include <unknwn.h>
#include "widget_card.hpp"
#include "widget_images.hpp"
#include <winrt/Microsoft.Windows.Widgets.Providers.h>
#include <filesystem>
#include <fstream>
#include <mutex>
namespace wp=winrt::Microsoft::Windows::Widgets::Providers;
static constexpr GUID ReviewClsid{0xae7791c6,0x45cd,0x4a7f,{0xb2,0x7f,0x53,0xd2,0xac,0x70,0x10,0x60}};
static HANDLE stop_review{};
struct ReviewRecord {WidgetSize size=WidgetSize::Large;WidgetView view;};
struct Review {
    std::recursive_mutex mutex;
    Snapshot state;
    WidgetIcons icons;
    bool logging{},high_contrast{};
    std::map<std::wstring,ReviewRecord> records;
    Review(){
        state.ready=state.fresh=state.noise_supported=state.ambient_supported=state.eq_supported=true;
        state.status="Connected";state.batteries={70,0,50};state.noise=2;state.ambient=20;state.preset=MDR_EQ_USER_2;state.bands={1,0,1,2,3};state.bass=4;state.firmware="1.0.5";state.checked=std::time(nullptr);
        for(int id:{26,25,0,8,9,10,11,12,13,14,15,24})state.presets.emplace_back(id,preset_name(id));
        wchar_t executable[32768]{};auto count=GetModuleFileNameW(nullptr,executable,32768);if(!count||count==32768)winrt::throw_last_error();
        auto folder=std::filesystem::path(executable).parent_path()/L"Public"/L"Icons";
        HIGHCONTRASTW contrast{sizeof(contrast)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);bool high=(contrast.dwFlags&HCF_HIGHCONTRASTON)!=0;high_contrast=high;auto foreground=GetSysColor(COLOR_WINDOWTEXT);bool white=GetRValue(foreground)+GetGValue(foreground)+GetBValue(foreground)>384;
        auto load=[&](const std::string& name){std::ifstream in(folder/name,std::ios::binary);if(!in)throw std::runtime_error("Missing review artwork.");std::string data((std::istreambuf_iterator<char>(in)),{});return png_uri({reinterpret_cast<const uint8_t*>(data.data()),data.size()});};
        for(auto name:{"case","noise-cancelling","ambient","off","equalizer","back","more","previous","next","plus","minus","check","bluetooth","bass","link","diagnostics","log"}){
            std::string prefix=name;auto light=high?(white?"hc-dark":"hc-light"):"light",dark=high?(white?"hc-dark":"hc-light"):"dark";
            icons.symbols[name]={load(prefix+"-"+light+".png"),load(prefix+"-"+dark+".png")};
            icons.symbols[prefix+"-selected"]=high?icons.symbols[name]:std::pair{load(prefix+"-accent-light.png"),load(prefix+"-accent.png")};
        }
        icons.light=icons.symbols["equalizer"].first;icons.dark=icons.symbols["equalizer"].second;
        for(int i=0;i<3;++i)icons.batteries[i]={battery_image(state.batteries[i],i==2,false,high),battery_image(state.batteries[i],i==2,true,high)};
        icons.marker={selection_marker(false,high),selection_marker(true,high)};
        icons.button_frames=button_frame_artwork(high);
        icons.border={border_image(false,high),border_image(true,high)};icons.surface={button_surface(false,high),button_surface(true,high)};icons.divider={divider_image(false,high),divider_image(true,high)};
        for(int i=0;i<2;++i)icons.switches[i]={switch_image(i!=0,false,high),switch_image(i!=0,true,high)};
        icons.battery_readouts=battery_readout_artwork(state.batteries,high);
        refresh_curves();
    }
    void refresh_curves(){icons.curves=curve_artwork(state.bands,high_contrast);icons.curve_light=icons.curves["large"].first;icons.curve_dark=icons.curves["large"].second;}
    void update(const std::wstring& id){auto& record=records.at(id);wp::WidgetUpdateRequestOptions request(id);request.Template(winrt::to_hstring(make_card(state,record.size,record.view,icons,logging)));request.Data(L"{}");wp::WidgetManager::GetDefault().UpdateWidget(request);}
    void context(wp::WidgetContext const& context){auto id=std::wstring(context.Id());records[id].size=context.Size();update(id);}
    void action(const std::wstring& id,const std::wstring& verb){
        if(!widget_eq_action_allowed(records.at(id).size,verb))return;
        auto& view=records.at(id).view;
        if(!navigate_widget(view,verb)){
            if(auto command=eq_command(view,state,verb)){
                if(!view.baseline&&(command->operation==Operation::Bands||command->operation==Operation::Bass))view.baseline=EqBaseline{state.preset,state.bass,state.bands};
                if(command->operation==Operation::Preset){state.preset=command->value;view.baseline.reset();state.bands=state.preset==26?std::vector<int32_t>{1,0,1,2,3}:std::vector<int32_t>{0,0,0,0,0};state.bass=state.preset==26?4:0;}
                else if(command->operation==Operation::Bands)state.bands=command->bands;
                else if(command->operation==Operation::Bass)state.bass=command->value;
                else if(command->operation==Operation::Reset){state.bands=command->bands;state.bass=command->bass;}
                view.page=WidgetPage::Equalizer;refresh_curves();
            }else if(suffix_number(verb,L"eq.preset.")==static_cast<int>(state.preset))view.page=WidgetPage::Equalizer;
            else if(verb==L"noise.anc"||verb==L"noise.ambient"||verb==L"noise.off"){state.noise=verb==L"noise.anc"?1:verb==L"noise.ambient"?2:0;if(view.page==WidgetPage::Noise)view.page=WidgetPage::Home;}
            else if(verb==L"ambient.up"||verb==L"ambient.down")state.ambient=std::clamp(state.ambient+(verb==L"ambient.up"?1:-1),1,20);
            else if(verb==L"pref.release")state.release_when_hidden=!state.release_when_hidden;
            else if(verb==L"pref.logging")logging=!logging;
        }
        update(id);
    }
};
struct ReviewProvider:winrt::implements<ReviewProvider,wp::IWidgetProvider,wp::IWidgetProvider2>{
    Review& review;explicit ReviewProvider(Review& owner):review(owner){}
    void CreateWidget(wp::WidgetContext context){std::lock_guard lock(review.mutex);review.context(context);}
    void DeleteWidget(winrt::hstring const& id,winrt::hstring const&){std::lock_guard lock(review.mutex);review.records.erase(std::wstring(id));}
    void OnActionInvoked(wp::WidgetActionInvokedArgs args){std::lock_guard lock(review.mutex);try{review.action(std::wstring(args.WidgetContext().Id()),std::wstring(args.Verb()));}catch(...) {}}
    void OnWidgetContextChanged(wp::WidgetContextChangedArgs args){std::lock_guard lock(review.mutex);review.context(args.WidgetContext());}
    void OnCustomizationRequested(wp::WidgetCustomizationRequestedArgs args){std::lock_guard lock(review.mutex);auto context=args.WidgetContext();auto id=std::wstring(context.Id());auto& record=review.records[id];record.size=context.Size();record.view.page=WidgetPage::More;record.view.details_page=0;review.update(id);}
    void Activate(wp::WidgetContext context){std::lock_guard lock(review.mutex);review.context(context);}
    void Deactivate(winrt::hstring const&){}
};
struct ReviewFactory:winrt::implements<ReviewFactory,IClassFactory>{
    Review& review;explicit ReviewFactory(Review& value):review(value){}
    HRESULT __stdcall CreateInstance(IUnknown* outer,REFIID iid,void** result) noexcept override{if(!result)return E_POINTER;*result=nullptr;if(outer)return CLASS_E_NOAGGREGATION;try{return winrt::make<ReviewProvider>(review).as(iid,result);}catch(...){return winrt::to_hresult();}}
    HRESULT __stdcall LockServer(BOOL) noexcept override{return S_OK;}
};
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){try{
    winrt::init_apartment(winrt::apartment_type::multi_threaded);stop_review=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stop_review)winrt::throw_last_error();
    Review review;auto factory=winrt::make<ReviewFactory>(review);DWORD cookie{};winrt::check_hresult(CoRegisterClassObject(ReviewClsid,factory.get(),CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE,&cookie));
    WaitForSingleObject(stop_review,INFINITE);CoRevokeClassObject(cookie);CloseHandle(stop_review);return 0;
}catch(...){return 1;}}
