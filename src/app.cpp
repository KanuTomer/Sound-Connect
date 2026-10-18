#include <windows.h>
#include <unknwn.h>
#include <appmodel.h>
#include "controller.hpp"
#include "widget_card.hpp"
#include "widget_images.hpp"
#include "native_editor.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.Windows.Widgets.Providers.h>
#include <winrt/Microsoft.Windows.Widgets.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <sddl.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <ctime>
#include <dbt.h>
#include <bthdef.h>

namespace wp=winrt::Microsoft::Windows::Widgets::Providers;
using winrt::Microsoft::Windows::Widgets::WidgetSize;
using namespace winrt::Windows::Data::Json;
constexpr GUID ProviderClsid{0x891e4d36,0xa7cb,0x423c,{0xb1,0x50,0x96,0xd9,0xeb,0x10,0x71,0xc7}};
constexpr UINT AppDispatchMessage=WM_APP+1, StateMessage=WM_APP+2;

static std::filesystem::path data_folder() {
    PWSTR raw{};winrt::check_hresult(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw));
    std::filesystem::path path(raw);CoTaskMemFree(raw);
    UINT32 length{};GetCurrentPackageFamilyName(&length,nullptr);
    if(length) { std::wstring family(length,L'\0');if(GetCurrentPackageFamilyName(&length,family.data())==ERROR_SUCCESS){family.resize(length-1);return path/L"Packages"/family/L"LocalState";} }
    return path/L"EarbudControlsDev";
}
static std::string read_file(const std::filesystem::path& path) {
    std::error_code ec;
    if(!std::filesystem::exists(path,ec) || std::filesystem::file_size(path,ec)>65536) return {};
    std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};
}
static Snapshot load_cache(const std::filesystem::path& path,bool& logging) {
    Snapshot s;
    try {
        auto text=read_file(path);if(text.empty())return s;
        auto json=JsonObject::Parse(winrt::to_hstring(text));
        logging=json.GetNamedBoolean(L"logging",false);
        s.release_when_hidden=json.GetNamedBoolean(L"release_when_hidden",false);
        s.address=winrt::to_string(json.GetNamedString(L"address",L""));
        s.firmware=winrt::to_string(json.GetNamedString(L"firmware",L""));
        s.checked=static_cast<int64_t>(json.GetNamedNumber(L"checked",0));
        s.noise=static_cast<int>(json.GetNamedNumber(L"noise",-1));
        s.ambient=static_cast<int>(json.GetNamedNumber(L"ambient",-1));s.voice=static_cast<int>(json.GetNamedNumber(L"voice",-1));
        s.noise_supported=json.GetNamedBoolean(L"noise_supported",false);
        s.ambient_supported=json.GetNamedBoolean(L"ambient_supported",false);
        s.eq_supported=json.GetNamedBoolean(L"eq_supported",false);
        s.preset=static_cast<uint32_t>(json.GetNamedNumber(L"preset",0));s.bass=static_cast<int>(json.GetNamedNumber(L"bass",0));
        if(json.HasKey(L"bands")){auto bands=json.GetNamedArray(L"bands");if(bands.Size()==5)for(auto value:bands)s.bands.push_back(static_cast<int32_t>(value.GetNumber()));}
        if(json.HasKey(L"presets")){auto presets=json.GetNamedArray(L"presets");if(presets.Size()<=64)for(auto value:presets){auto preset=value.GetObject();s.presets.emplace_back(static_cast<uint32_t>(preset.GetNamedNumber(L"id")),winrt::to_string(preset.GetNamedString(L"name")));}}
        s.batteries={static_cast<int>(json.GetNamedNumber(L"left",-1)),static_cast<int>(json.GetNamedNumber(L"right",-1)),static_cast<int>(json.GetNamedNumber(L"case",-1))};
        s.status=s.checked?"Controls idle":"Not connected";
    }catch(...){}
    return s;
}
static std::string icon_data(const std::filesystem::path& path){
    auto bytes=read_file(path);if(bytes.empty())return {};
    return png_uri({reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size()});
}
struct WidgetRecord { WidgetSize size=WidgetSize::Medium;bool active{};WidgetView view;std::string last_template; };
class App;
struct WidgetProvider : winrt::implements<WidgetProvider,wp::IWidgetProvider,wp::IWidgetProvider2> {
    App& app;
    explicit WidgetProvider(App& owner);
    ~WidgetProvider();
    void CreateWidget(wp::WidgetContext context);
    void DeleteWidget(winrt::hstring const& id,winrt::hstring const&);
    void OnActionInvoked(wp::WidgetActionInvokedArgs args);
    void OnWidgetContextChanged(wp::WidgetContextChangedArgs args);
    void OnCustomizationRequested(wp::WidgetCustomizationRequestedArgs args);
    void Activate(wp::WidgetContext context);
    void Deactivate(winrt::hstring id);
};

class App {
public:
    HWND dispatcher{},editor{};
    std::unique_ptr<Controller> controller;
    std::atomic<int> references{};
    std::map<std::wstring,WidgetRecord> widgets;
    WidgetIcons icons;
    std::filesystem::path assets_path;
    std::string curve_key;
    std::string battery_key;
    bool artwork_loaded{},artwork_contrast{};
    COLORREF artwork_text{};
    HPOWERNOTIFY power_notification{};
    HDEVNOTIFY interface_notification{};
    std::vector<std::pair<HANDLE,HDEVNOTIFY>> radio_notifications;
    bool logging{}, provider_started{}, editor_started{};
    std::filesystem::path folder;
    std::mutex dispatch_mutex;
    std::deque<std::function<void()>> tasks;
    HANDLE stop_pipe=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::thread pipe_thread;
    std::wstring pipe_name;
    std::unique_ptr<NativeEditor> native;
    App():folder(data_folder()){}
    ~App(){if(power_notification)UnregisterSuspendResumeNotification(power_notification);if(interface_notification)UnregisterDeviceNotification(interface_notification);for(auto [radio,notice]:radio_notifications){if(notice)UnregisterDeviceNotification(notice);CloseHandle(radio);}SetEvent(stop_pipe);if(pipe_thread.joinable())pipe_thread.join();native.reset();controller.reset();if(dispatcher)DestroyWindow(dispatcher);CloseHandle(stop_pipe);}
    void post(std::function<void()> fn){ {std::lock_guard lock(dispatch_mutex);tasks.push_back(std::move(fn));}PostMessageW(dispatcher,AppDispatchMessage,0,0); }
    void dispatch(){std::deque<std::function<void()>> work;{std::lock_guard lock(dispatch_mutex);work.swap(tasks);}for(auto& fn:work){try{fn();}catch(const winrt::hresult_error& e){diagnostic(winrt::to_string(e.message()));}catch(const std::exception& e){diagnostic(e.what());}}}
    void diagnostic(const std::string& text){if(!logging)return;try{auto path=folder/L"diagnostics.log";std::error_code ec;bool reset=std::filesystem::exists(path,ec)&&std::filesystem::file_size(path,ec)>5*1024*1024;std::ofstream out(path,reset?std::ios::trunc:std::ios::app);out<<checked_time(std::time(nullptr))<<" "<<text.substr(0,2048)<<"\n";}catch(...){}}
    void init(bool show) {
        wchar_t module[32768]{};auto count=GetModuleFileNameW(nullptr,module,32768);
        if(count&&count<32768)assets_path=std::filesystem::path(module).parent_path()/L"Public"/L"Icons";
        WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"EarbudControls.Dispatcher";
        RegisterClassW(&wc);dispatcher=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,this);
        if(!dispatcher)winrt::throw_last_error();
        auto initial=load_cache(folder/L"state.json",logging);
        std::filesystem::create_directories(folder);
        controller=std::make_unique<Controller>([this]{PostMessageW(dispatcher,StateMessage,0,0);},std::move(initial));
        power_notification=RegisterSuspendResumeNotification(dispatcher,DEVICE_NOTIFY_WINDOW_HANDLE);
        DEV_BROADCAST_DEVICEINTERFACE_W filter{};filter.dbcc_size=sizeof(filter);filter.dbcc_devicetype=DBT_DEVTYP_DEVICEINTERFACE;filter.dbcc_classguid=GUID_BTHPORT_DEVICE_INTERFACE;
        interface_notification=RegisterDeviceNotificationW(dispatcher,&filter,DEVICE_NOTIFY_WINDOW_HANDLE);
        watch_radios();
        if(show)open(0);
        SetTimer(dispatcher,1,30000,nullptr);
    }
    void interest(){bool active=editor!=nullptr;for(auto& [id,widget]:widgets)active|=widget.active;controller->interest(active);if(!active){icons.curve_light.clear();icons.curve_dark.clear();icons.curves.clear();curve_key.clear();}}
    void recover(){
        try{for(auto info:wp::WidgetManager::GetDefault().GetWidgetInfos()){auto context=info.WidgetContext();widgets[std::wstring(context.Id())].size=context.Size();}update_widgets(true);}
        catch(const winrt::hresult_error& e){diagnostic(winrt::to_string(e.message()));}
    }
    void artwork(const Snapshot& state){
        HIGHCONTRASTW contrast{sizeof(contrast)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);
        bool high=(contrast.dwFlags&HCF_HIGHCONTRASTON)!=0;auto foreground=GetSysColor(COLOR_WINDOWTEXT);
        if(!artwork_loaded||artwork_contrast!=high||artwork_text!=foreground){
            for(auto name:{"case","settings","noise-cancelling","ambient","off","equalizer","back","more","previous","next","plus","minus","check","bluetooth","bass","link","diagnostics","log"}){
                bool white=GetRValue(foreground)+GetGValue(foreground)+GetBValue(foreground)>384;
                std::string light=std::string(name)+(high?(white?"-hc-dark.png":"-hc-light.png"):"-light.png"),dark=std::string(name)+(high?(white?"-hc-dark.png":"-hc-light.png"):"-dark.png");
                icons.symbols[name]={icon_data(assets_path/light),icon_data(assets_path/dark)};
                icons.symbols[std::string(name)+"-selected"]=high?icons.symbols[name]:std::pair{icon_data(assets_path/(std::string(name)+"-accent-light.png")),icon_data(assets_path/(std::string(name)+"-accent.png"))};
            }
            icons.light=icons.symbols["equalizer"].first;icons.dark=icons.symbols["equalizer"].second;
            icons.marker={selection_marker(false,high),selection_marker(true,high)};
        icons.border={border_image(false,high),border_image(true,high)};icons.surface={button_surface(false,high),button_surface(true,high)};
            for(int i=0;i<2;++i)icons.switches[i]={switch_image(i!=0,false,high),switch_image(i!=0,true,high)};
            artwork_loaded=true;artwork_contrast=high;artwork_text=foreground;curve_key.clear();battery_key.clear();
        }
        auto batteries=std::to_string(state.batteries[0])+":"+std::to_string(state.batteries[1])+":"+std::to_string(state.batteries[2]);
        if(batteries!=battery_key){for(int i=0;i<3;++i)icons.batteries[i]={battery_image(state.batteries[i],i==2,false,high),battery_image(state.batteries[i],i==2,true,high)};battery_key=std::move(batteries);}
        bool curve=false;for(auto& [id,record]:widgets)curve|=record.active&&((record.view.page==WidgetPage::Equalizer&&record.size!=WidgetSize::Small)||(record.view.page==WidgetPage::Home&&record.size==WidgetSize::Large));
        if(curve&&state.bands.size()==5){
            COLORREF accent=high?GetSysColor(COLOR_HIGHLIGHT):RGB(76,194,255);
            std::string key=std::to_string(accent)+":"+std::to_string(high);for(auto band:state.bands)key+=":"+std::to_string(band);
            if(key!=curve_key){icons.curves=curve_artwork(state.bands,high);icons.curve_light=icons.curves["large"].first;icons.curve_dark=icons.curves["large"].second;curve_key=key;}
        }
    }
    void update_widgets(bool force=false){auto s=controller->snapshot();artwork(s);for(auto& [id,record]:widgets){reconcile_widget(record.view,s);if(!force&&!record.active)continue;try{auto card=make_card(s,record.size,record.view,icons,logging);if(!force&&card==record.last_template)continue;wp::WidgetUpdateRequestOptions update(id);update.Template(winrt::to_hstring(card));update.Data(L"{}");wp::WidgetManager::GetDefault().UpdateWidget(update);record.last_template=std::move(card);}catch(const winrt::hresult_error& e){diagnostic(winrt::to_string(e.message()));}}}
    void changed(){
        auto state=controller->snapshot();update_widgets();refresh_editor();
        if(state.checked&&!state.pending)save(state);
        if(!state.error.empty())diagnostic(state.error);
        maybe_exit();
    }
    void save(const Snapshot& s){
        try{JsonObject j;j.SetNamedValue(L"address",JsonValue::CreateStringValue(winrt::to_hstring(s.address)));j.SetNamedValue(L"firmware",JsonValue::CreateStringValue(winrt::to_hstring(s.firmware)));
            j.SetNamedValue(L"logging",JsonValue::CreateBooleanValue(logging));j.SetNamedValue(L"checked",JsonValue::CreateNumberValue(static_cast<double>(s.checked)));
            j.SetNamedValue(L"release_when_hidden",JsonValue::CreateBooleanValue(s.release_when_hidden));
            j.SetNamedValue(L"noise",JsonValue::CreateNumberValue(s.noise));j.SetNamedValue(L"ambient",JsonValue::CreateNumberValue(s.ambient));j.SetNamedValue(L"voice",JsonValue::CreateNumberValue(s.voice));
            j.SetNamedValue(L"noise_supported",JsonValue::CreateBooleanValue(s.noise_supported));j.SetNamedValue(L"ambient_supported",JsonValue::CreateBooleanValue(s.ambient_supported));j.SetNamedValue(L"eq_supported",JsonValue::CreateBooleanValue(s.eq_supported));
            j.SetNamedValue(L"preset",JsonValue::CreateNumberValue(s.preset));j.SetNamedValue(L"bass",JsonValue::CreateNumberValue(s.bass));
            JsonArray bands;for(auto band:s.bands)bands.Append(JsonValue::CreateNumberValue(band));j.SetNamedValue(L"bands",bands);
            JsonArray presets;for(auto& [id,name]:s.presets){JsonObject preset;preset.SetNamedValue(L"id",JsonValue::CreateNumberValue(id));preset.SetNamedValue(L"name",JsonValue::CreateStringValue(winrt::to_hstring(name)));presets.Append(preset);}j.SetNamedValue(L"presets",presets);
            const wchar_t* keys[]={L"left",L"right",L"case"};for(int i=0;i<3;++i)j.SetNamedValue(keys[i],JsonValue::CreateNumberValue(s.batteries[i]));
            auto temporary=folder/L"state.tmp";{std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out<<winrt::to_string(j.Stringify());if(!out)throw std::runtime_error("Could not save settings.");}
            if(!MoveFileExW(temporary.c_str(),(folder/L"state.json").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))winrt::throw_last_error();
        }catch(const std::exception& e){diagnostic(e.what());}catch(const winrt::hresult_error& e){diagnostic(winrt::to_string(e.message()));}
    }
    void maybe_exit(){if(!editor && references==0 && (provider_started||editor_started) && !controller->snapshot().ready && !controller->snapshot().pending)PostQuitMessage(0);}
    void action(const std::wstring& id,const std::wstring& verb){
        auto widget=widgets.find(id);if(widget==widgets.end())return;
        if(!widget_eq_action_allowed(widget->second.size,verb))return;
        widget->second.active=true;interest();
        if(navigate_widget(widget->second.view,verb)){update_widgets();return;}
        auto state=controller->snapshot();auto& view=widget->second.view;
        if(auto command=eq_command(view,state,verb)){
            if(controller->send(*command)){
                if(!view.baseline&&(command->operation==Operation::Bands||command->operation==Operation::Bass))view.baseline=EqBaseline{state.preset,state.bass,state.bands};
                // Only the Presets list returns to the EQ view; Home chips stay put.
                view.proposed=*command;view.failed_write=false;if(view.page==WidgetPage::Presets)view.page=WidgetPage::Equalizer;
                if(command->operation==Operation::Preset)view.baseline.reset();
                update_widgets();
            }
            return;
        }
        if(suffix_number(verb,L"eq.preset.")==static_cast<int>(state.preset)){if(view.page==WidgetPage::Presets)view.page=WidgetPage::Equalizer;update_widgets();return;}
        if(verb==L"pref.release"){controller->release_when_hidden(!state.release_when_hidden);save(controller->snapshot());update_widgets();return;}
        if(verb==L"pref.logging"){logging=!logging;save(state);update_widgets();return;}
        if(verb==L"app.exit"&&!state.pending){PostQuitMessage(0);return;}
        if(verb==L"open.bluetooth"){ShellExecuteW(nullptr,L"open",L"ms-settings:bluetooth",nullptr,nullptr,SW_SHOWNORMAL);return;}
        if(verb==L"retry")controller->send({Operation::Retry});
        else if(verb==L"noise.anc"||verb==L"noise.ambient"||verb==L"noise.off"){
            int mode=verb==L"noise.anc"?1:verb==L"noise.ambient"?2:0;
            if(controller->send({Operation::Noise,mode})&&view.page==WidgetPage::Noise){view.page=WidgetPage::Home;update_widgets();}
        }
        else if(verb==L"ambient.up"||verb==L"ambient.down"){auto s=controller->snapshot();controller->send({Operation::Ambient,std::clamp(s.ambient+(verb==L"ambient.up"?1:-1),1,20)});}
        else if(verb==L"ambient.voice"){auto s=controller->snapshot();controller->send({Operation::Voice,s.voice==1?0:1});}
        else if(verb==L"open.sound")open(0);
        else if(verb==L"open.settings")open(2);
    }
    void open(int page){
        editor_started=true;
        if(native&&native->window){native->show(page);return;}
        native=std::make_unique<NativeEditor>([this]{return controller->snapshot();},[this](Command command){controller->send(std::move(command));},
            [this]{editor=nullptr;interest();post([this]{if(native&&!native->window)native.reset();maybe_exit();});},
            [this]{return logging;},[this](bool enabled){logging=enabled;save(controller->snapshot());});
        editor=native->window;native->show(page);interest();
    }
    void refresh_editor(){if(native)native->refresh();}
    void watch_radios(){
        for(auto [radio,notice]:radio_notifications){if(notice)UnregisterDeviceNotification(notice);CloseHandle(radio);}radio_notifications.clear();
        BLUETOOTH_FIND_RADIO_PARAMS search{sizeof(search)};HANDLE radio{};
        auto found=BluetoothFindFirstRadio(&search,&radio);if(!found)return;
        do{DEV_BROADCAST_HANDLE filter{};filter.dbch_size=sizeof(filter);filter.dbch_devicetype=DBT_DEVTYP_HANDLE;filter.dbch_handle=radio;auto notice=RegisterDeviceNotificationW(dispatcher,&filter,DEVICE_NOTIFY_WINDOW_HANDLE);radio_notifications.emplace_back(radio,notice);}while(BluetoothFindNextRadio(found,&radio));
        BluetoothFindRadioClose(found);
    }
    bool remembered(BTH_ADDR value){
        char address[18]{};snprintf(address,sizeof(address),"%02X:%02X:%02X:%02X:%02X:%02X",static_cast<unsigned>((value>>40)&255),static_cast<unsigned>((value>>32)&255),static_cast<unsigned>((value>>24)&255),static_cast<unsigned>((value>>16)&255),static_cast<unsigned>((value>>8)&255),static_cast<unsigned>(value&255));return controller->snapshot().address==address;
    }
    void device_event(WPARAM event,LPARAM data){
        if(!data)return;auto* header=reinterpret_cast<DEV_BROADCAST_HDR*>(data);
        if(event==DBT_CUSTOMEVENT&&header->dbch_devicetype==DBT_DEVTYP_HANDLE){
            auto* notification=reinterpret_cast<DEV_BROADCAST_HANDLE*>(data);
            if(header->dbch_size<offsetof(DEV_BROADCAST_HANDLE,dbch_data)+sizeof(BTH_HCI_EVENT_INFO))return;
            if(notification->dbch_eventguid==GUID_BLUETOOTH_HCI_EVENT){BTH_HCI_EVENT_INFO info{};std::memcpy(&info,notification->dbch_data,sizeof(info));if(info.connectionType==HCI_CONNECTION_TYPE_ACL&&remembered(info.bthAddress))controller->lifecycle(info.connected?Operation::Reconnect:Operation::Disconnect);}
        }else if(header->dbch_devicetype==DBT_DEVTYP_DEVICEINTERFACE&&(event==DBT_DEVICEARRIVAL||event==DBT_DEVICEREMOVECOMPLETE)){
            if(event==DBT_DEVICEREMOVECOMPLETE)controller->lifecycle(Operation::Disconnect);
            watch_radios();
        }
    }
    void start_pipe(PSECURITY_DESCRIPTOR descriptor){
        pipe_thread=std::thread([this,descriptor]{
            SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};
            for(;;){
                HANDLE pipe=CreateNamedPipeW(pipe_name.c_str(),PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,64,64,0,&attributes);
                if(pipe==INVALID_HANDLE_VALUE)return;
                HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);OVERLAPPED ov{};ov.hEvent=event;
                BOOL connected=ConnectNamedPipe(pipe,&ov);DWORD error=connected?ERROR_SUCCESS:GetLastError();
                HANDLE waits[]={stop_pipe,event};
                bool ready=connected||error==ERROR_PIPE_CONNECTED;
                if(error==ERROR_IO_PENDING)ready=WaitForMultipleObjects(2,waits,FALSE,INFINITE)==WAIT_OBJECT_0+1;
                if(ready){ResetEvent(event);DWORD page{},bytes{};BOOL read=ReadFile(pipe,&page,sizeof(page),&bytes,&ov);if(!read&&GetLastError()==ERROR_IO_PENDING&&WaitForMultipleObjects(2,waits,FALSE,1000)==WAIT_OBJECT_0+1)read=GetOverlappedResult(pipe,&ov,&bytes,FALSE);
                    if(read&&bytes==sizeof(page)&&page<=2)post([this,page]{open(static_cast<int>(page));});}
                CancelIoEx(pipe,nullptr);DisconnectNamedPipe(pipe);CloseHandle(pipe);CloseHandle(event);
                if(WaitForSingleObject(stop_pipe,0)==WAIT_OBJECT_0)return;
            }
        });
    }
    static LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam){
        auto* self=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,wparam,lparam);
        try{
            if(message==AppDispatchMessage){self->dispatch();return 0;}
            if(message==StateMessage){self->changed();return 0;}
            if(message==WM_DEVICECHANGE){self->device_event(wparam,lparam);return TRUE;}
            if(message==WM_POWERBROADCAST){if(wparam==PBT_APMSUSPEND)self->controller->lifecycle(Operation::Suspend);else if(wparam==PBT_APMRESUMEAUTOMATIC||wparam==PBT_APMRESUMESUSPEND)self->controller->lifecycle(Operation::Reconnect);return TRUE;}
            if(message==WM_TIMER&&window==self->dispatcher){KillTimer(window,1);self->maybe_exit();return 0;}
        }catch(const winrt::hresult_error& e){self->diagnostic(winrt::to_string(e.message()));}catch(const std::exception& e){self->diagnostic(e.what());}
        return DefWindowProcW(window,message,wparam,lparam);
    }
};

WidgetProvider::WidgetProvider(App& owner):app(owner){++app.references;app.post([&owner]{owner.provider_started=true;owner.recover();});}
WidgetProvider::~WidgetProvider(){--app.references;app.post([owner=&app]{owner->maybe_exit();});}
void WidgetProvider::CreateWidget(wp::WidgetContext context){auto id=std::wstring(context.Id());auto size=context.Size();app.post([owner=&app,id,size]{owner->widgets[id]={size,false};owner->update_widgets();});}
void WidgetProvider::DeleteWidget(winrt::hstring const& id,winrt::hstring const&){app.post([owner=&app,id=std::wstring(id)]{owner->widgets.erase(id);owner->interest();});}
void WidgetProvider::OnActionInvoked(wp::WidgetActionInvokedArgs args){auto id=std::wstring(args.WidgetContext().Id());auto verb=std::wstring(args.Verb());app.post([owner=&app,id,verb]{owner->action(id,verb);});}
void WidgetProvider::OnWidgetContextChanged(wp::WidgetContextChangedArgs args){auto context=args.WidgetContext();auto id=std::wstring(context.Id());auto size=context.Size();app.post([owner=&app,id,size]{owner->widgets[id].size=size;owner->update_widgets();});}
void WidgetProvider::OnCustomizationRequested(wp::WidgetCustomizationRequestedArgs args){auto context=args.WidgetContext();auto id=std::wstring(context.Id());auto size=context.Size();app.post([owner=&app,id,size]{auto& widget=owner->widgets[id];widget.size=size;widget.active=true;widget.view.page=WidgetPage::More;widget.view.details_page=0;owner->interest();owner->update_widgets();});}
void WidgetProvider::Activate(wp::WidgetContext context){auto id=std::wstring(context.Id());auto size=context.Size();app.post([owner=&app,id,size]{auto& widget=owner->widgets[id];widget.size=size;widget.active=true;owner->interest();owner->update_widgets();});}
void WidgetProvider::Deactivate(winrt::hstring id){app.post([owner=&app,id=std::wstring(id)]{auto found=owner->widgets.find(id);if(found!=owner->widgets.end())found->second.active=false;owner->interest();});}

struct ProviderFactory:winrt::implements<ProviderFactory,IClassFactory>{
    App& app;explicit ProviderFactory(App& owner):app(owner){}
    HRESULT __stdcall CreateInstance(IUnknown* outer,REFIID iid,void** value) noexcept override{if(!value)return E_POINTER;*value=nullptr;if(outer)return CLASS_E_NOAGGREGATION;try{return winrt::make<WidgetProvider>(app).as(iid,value);}catch(...){return winrt::to_hresult();}}
    HRESULT __stdcall LockServer(BOOL lock) noexcept override{if(lock)++app.references;else --app.references;return S_OK;}
};

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR arguments,int){
    bool provider=wcsstr(arguments,L"--provider")||wcsstr(arguments,L"-Embedding");
    try{
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&common);
        HANDLE token{};winrt::check_bool(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token));DWORD bytes{};GetTokenInformation(token,TokenUser,nullptr,0,&bytes);std::vector<BYTE> buffer(bytes);winrt::check_bool(GetTokenInformation(token,TokenUser,buffer.data(),bytes,&bytes));CloseHandle(token);
        LPWSTR raw_sid{};winrt::check_bool(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&raw_sid));std::wstring sid(raw_sid);LocalFree(raw_sid);
        PSECURITY_DESCRIPTOR descriptor{};winrt::check_bool(ConvertStringSecurityDescriptorToSecurityDescriptorW((L"D:P(A;;GA;;;"+sid+L")").c_str(),SDDL_REVISION_1,&descriptor,nullptr));
        SECURITY_ATTRIBUTES attributes{sizeof(attributes),descriptor,FALSE};DWORD session_id{};ProcessIdToSessionId(GetCurrentProcessId(),&session_id);
        auto suffix=sid+L"."+std::to_wstring(session_id);auto pipe=L"\\\\.\\pipe\\EarbudControls."+suffix;
        HANDLE mutex=CreateMutexW(&attributes,FALSE,(L"Local\\EarbudControls."+suffix).c_str());if(!mutex)winrt::throw_last_error();
        if(GetLastError()==ERROR_ALREADY_EXISTS){
            if(!provider){auto deadline=Clock::now()+2s;while(Clock::now()<deadline){HANDLE client=CreateFileW(pipe.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);if(client!=INVALID_HANDLE_VALUE){DWORD page=0,written{};WriteFile(client,&page,sizeof(page),&written,nullptr);CloseHandle(client);break;}std::this_thread::sleep_for(50ms);}}
            LocalFree(descriptor);CloseHandle(mutex);return 0;
        }
        {
            App app;app.pipe_name=pipe;app.init(!provider);app.start_pipe(descriptor);
            auto factory=winrt::make<ProviderFactory>(app);DWORD cookie{};
            winrt::check_hresult(CoRegisterClassObject(ProviderClsid,factory.get(),CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE,&cookie));
            MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(app.editor&&IsDialogMessageW(app.editor,&msg))continue;TranslateMessage(&msg);DispatchMessageW(&msg);}
            CoRevokeClassObject(cookie);
        }
        LocalFree(descriptor);CloseHandle(mutex);return 0;
    }catch(const winrt::hresult_error& e){if(!provider)MessageBoxW(nullptr,e.message().c_str(),L"Earbud Controls",MB_ICONERROR);return 1;}
    catch(const std::exception& e){if(!provider)MessageBoxW(nullptr,winrt::to_hstring(e.what()).c_str(),L"Earbud Controls",MB_ICONERROR);return 1;}
}


