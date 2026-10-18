#pragma once
#include "controller.hpp"
#include <winrt/Microsoft.Windows.Widgets.h>
using winrt::Microsoft::Windows::Widgets::WidgetSize;
#include <map>
#include <optional>

enum class WidgetPage { Home, Equalizer, Presets, More, Diagnostics, Noise };
struct EqBaseline {uint32_t preset{};int bass{};std::vector<int32_t> bands;};
struct WidgetView {
    WidgetPage page=WidgetPage::Home;
    int band{},preset_page{},details_page{};
    std::optional<EqBaseline> baseline;
    std::optional<Command> proposed;
    bool failed_write{};
};
struct WidgetIcons {
    std::string light,dark;
    std::map<std::string,std::pair<std::string,std::string>> symbols;
    std::string curve_light,curve_dark;
    std::map<std::string,std::pair<std::string,std::string>> curves;
    std::array<std::pair<std::string,std::string>,3> batteries;
    std::array<std::pair<std::string,std::string>,2> switches;
    std::pair<std::string,std::string> marker,surface,divider,border;
    std::map<int,std::pair<std::string,std::string>> button_frames;
    std::map<std::string,std::array<std::pair<std::string,std::string>,3>> battery_readouts;
};
static bool battery_low(int value){return value>=0&&value<=20;}
static bool widget_eq_action_allowed(WidgetSize size,std::wstring_view verb){return size!=WidgetSize::Small||!(verb.starts_with(L"eq.")||verb==L"view.eq"||verb==L"view.presets"||verb.starts_with(L"view.presets.")||verb.starts_with(L"view.band."));}
static bool editable_eq(const Snapshot& s){return s.eq_supported&&s.bands.size()==5&&(s.preset==MDR_EQ_CUSTOM||s.preset==MDR_EQ_USER_1||s.preset==MDR_EQ_USER_2);}
static bool navigate_widget(WidgetView& view,std::wstring_view verb) {
    if(verb==L"view.eq")view.page=WidgetPage::Equalizer;
    else if(verb==L"view.home")view.page=WidgetPage::Home;
    else if(verb==L"view.more"){view.page=WidgetPage::More;view.details_page=0;}
    else if(verb==L"view.noise")view.page=WidgetPage::Noise;
    else if(verb==L"view.presets"){view.page=WidgetPage::Presets;view.preset_page=0;}
    else if(verb==L"view.diagnostics"){view.page=WidgetPage::Diagnostics;view.details_page=0;}
    else if(verb==L"view.details.next")++view.details_page;
    else if(verb==L"view.details.previous")view.details_page=std::max(0,view.details_page-1);
    else if(verb==L"view.presets.next")++view.preset_page;
    else if(verb==L"view.presets.previous")view.preset_page=std::max(0,view.preset_page-1);
    else if(verb==L"view.band.next")view.band=(view.band+1)%5;
    else if(verb==L"view.band.previous")view.band=(view.band+4)%5;
    else if(verb.starts_with(L"view.band.")&&verb.size()==11&&verb.back()>=L'0'&&verb.back()<=L'4')view.band=verb.back()-L'0';
    else return false;
    return true;
}
static void reconcile_widget(WidgetView& view,const Snapshot& state){
    if(view.proposed&&!state.pending){view.failed_write=!state.error.empty();view.proposed.reset();}
    if(!state.pending&&state.ready&&state.fresh&&state.error.empty())view.failed_write=false;
    if(view.baseline&&!state.pending&&state.fresh&&view.baseline->preset!=state.preset)view.baseline.reset();
}
static int suffix_number(std::wstring_view verb,std::wstring_view prefix){
    if(!verb.starts_with(prefix)||verb.size()==prefix.size())return -1;
    int value{};for(auto ch:verb.substr(prefix.size())){if(ch<L'0'||ch>L'9'||value>100)return -1;value=value*10+(ch-L'0');}return value;
}
static std::optional<Command> eq_command(const WidgetView& view,const Snapshot& state,std::wstring_view verb){
    if(!state.ready||!state.fresh||state.pending||!state.eq_supported)return {};
    auto preset=suffix_number(verb,L"eq.preset.");
    if(preset>=0){
        if(std::none_of(state.presets.begin(),state.presets.end(),[&](auto& p){return p.first==static_cast<uint32_t>(preset);})||state.preset==static_cast<uint32_t>(preset))return {};
        return Command{Operation::Preset,preset};
    }
    if(!editable_eq(state))return {};
    if(verb==L"eq.reset"){
        if(!view.baseline||view.baseline->preset!=state.preset||view.baseline->bands.size()!=5)return {};
        Command command{Operation::Reset};command.preset=view.baseline->preset;command.bass=view.baseline->bass;command.bands=view.baseline->bands;return command;
    }
    int band=suffix_number(verb,L"eq.increase."),step=1;
    if(band<0){band=suffix_number(verb,L"eq.decrease.");step=-1;}
    if(band<0||band>5)return {};
    int current=band==5?state.bass:state.bands[band];
    int value=std::clamp(current+step,-10,10);if(value==current)return {};
    if(band==5)return Command{Operation::Bass,value};
    Command command{Operation::Bands};command.bands=state.bands;command.bands[band]=value;return command;
}
