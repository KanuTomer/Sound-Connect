#pragma once
#include "widget_state.hpp"
#include <winrt/Microsoft.Windows.Widgets.h>
#include <ctime>
using winrt::Microsoft::Windows::Widgets::WidgetSize;

static std::string checked_time(int64_t seconds){
    if(!seconds)return "Not checked yet";
    std::time_t value=seconds;std::tm local{};localtime_s(&local,&value);
    char text[64]{};std::strftime(text,sizeof(text),"Last seen %H:%M",&local);return text;
}
static std::string battery_text(const Snapshot& s){
    auto percent=[](int n){return n>=0&&n<=100?std::to_string(n)+"%":"--";};
    return "L "+percent(s.batteries[0])+"    R "+percent(s.batteries[1])+"    Case "+percent(s.batteries[2]);
}
static const char* mode_name(int mode){return mode==1?"Noise cancelling":mode==2?"Ambient sound":mode==0?"Off":"Not checked";}
static std::string signed_value(int value){return (value>0?"+":"")+std::to_string(value);}
namespace card {
using Items=std::vector<std::string>;
static std::string list(const Items& items){std::string result="[";for(auto& item:items){if(item.empty())continue;if(result.size()>1)result+=",";result+=item;}return result+"]";}
static std::string theme(const std::pair<std::string,std::string>& urls){return "${$host.hostTheme == 'dark' ? '"+urls.second+"' : '"+urls.first+"'}";}
static std::string text(const std::string& value,const char* size="Small",bool subtle=false,bool bold=false,const char* align="Left",int lines=2,const char* color="Default"){
    return "{\"type\":\"TextBlock\",\"spacing\":\"None\",\"text\":"+::quoted(value)+",\"size\":"+::quoted(size)+",\"color\":"+::quoted(color)+",\"isSubtle\":"+(subtle?"true":"false")+",\"weight\":"+::quoted(bold?"Bolder":"Default")+",\"wrap\":true,\"maxLines\":"+std::to_string(lines)+",\"horizontalAlignment\":"+::quoted(align)+"}";
}
static std::string action(const std::string& verb,const std::string& label,bool enabled=true,const std::string& id=""){
    return "{\"type\":\"Action.Execute\",\"id\":"+::quoted(id.empty()?verb:id)+",\"verb\":"+::quoted(verb)+",\"title\":"+::quoted(label)+",\"isEnabled\":"+(enabled?"true":"false")+"}";
}
static std::string box(const Items& items,int height=0,const std::string& click="",const std::string& background="",const char* type="Container"){
    return "{\"type\":"+::quoted(type)+",\"spacing\":\"None\",\"verticalContentAlignment\":\"Center\",\"items\":"+list(items)+(height?",\"minHeight\":"+::quoted(std::to_string(height)+"px"):"")+(click.empty()?"":",\"selectAction\":"+click)+(background.empty()?"":",\"style\":"+::quoted(background))+"}";
}
static std::string col(const Items& items,const std::string& width="stretch",int height=0,const std::string& click="",const std::string& background=""){
    auto result=box(items,height,click,background,"Column");
    return result.substr(0,result.size()-1)+",\"width\":"+::quoted(width)+"}";
}
static std::string columns(const Items& items,int height=0){return "{\"type\":\"ColumnSet\",\"spacing\":\"None\",\"columns\":"+list(items)+(height?",\"minHeight\":"+::quoted(std::to_string(height)+"px"):"")+"}";}
static std::string gap(int height){return box({},height);}
static std::string pad(int width){return col({},std::to_string(width)+"px");}
static std::string image(const std::pair<std::string,std::string>& urls,int width,int height,const std::string& alt=""){
    if(urls.first.empty()||urls.second.empty())return {};
    return "{\"type\":\"Image\",\"spacing\":\"None\",\"url\":"+::quoted(theme(urls))+",\"width\":"+::quoted(std::to_string(width)+"px")+",\"height\":"+::quoted(std::to_string(height)+"px")+",\"horizontalAlignment\":\"Center\",\"altText\":"+::quoted(alt)+"}";
}
static std::pair<std::string,std::string> curve_art(const WidgetIcons& icons,const char* name){auto found=icons.curves.find(name);return found==icons.curves.end()?std::pair{icons.curve_light,icons.curve_dark}:found->second;}
static std::string glyph(const WidgetIcons& icons,const char* name,int size=20){auto found=icons.symbols.find(name);return found==icons.symbols.end()?"":image(found->second,size,size);}
static std::string flat(const Items& items,int height,const std::string& click,const WidgetIcons& icons,const char* type="Container",bool selected=false){
    auto result=box(items,height,click,"",type);
    auto frame=icons.button_frames.find(height+(selected?1000:0));
    if(frame!=icons.button_frames.end())result=result.substr(0,result.size()-1)+",\"backgroundImage\":{\"url\":"+::quoted(theme(frame->second))+",\"fillMode\":\"cover\"}}";
    return result;
}
static std::string icon_button(const WidgetIcons& icons,const char* name,const std::string& verb,const std::string& label,bool enabled=true,int size=24){
    bool step=verb.starts_with("eq.increase.")||verb.starts_with("eq.decrease.")||verb.starts_with("ambient.");
    if(!step)return col({glyph(icons,name,18)},std::to_string(size)+"px",size,action(verb,label,enabled));
    int inset=(size-18)/2;
    // Columns stretch to the tallest item in their row. Keep the framed
    // click target inside the column so a taller bass/value row cannot crop it.
    return col({flat({gap(inset),glyph(icons,name,18),gap(inset)},size,action(verb,label,enabled),icons)},std::to_string(size)+"px",size);
}
}
static std::string make_card(const Snapshot& s,WidgetSize size,const WidgetView& view={},const WidgetIcons& icons={},bool logging=false){
    using namespace card;
    const bool compact=size==WidgetSize::Small,expanded=size==WidgetSize::Large,enabled=s.ready&&s.fresh&&!s.pending;
    const bool curve_known=s.eq_supported&&s.bands.size()==5&&s.preset!=MDR_EQ_OFF;
    const char* names[]={"400 Hz","1 kHz","2.5 kHz","6.3 kHz","16 kHz","Clear Bass"};
    const char* short_names[]={"400","1k","2.5k","6.3k","16k"};
    const int band=std::clamp(view.band,0,4);
    const auto page=compact&&(view.page==WidgetPage::Equalizer||view.page==WidgetPage::Presets||view.page==WidgetPage::Noise)?WidgetPage::Home:view.page;
    Items body;
    auto add=[&](std::string item,int before=0){if(before)body.push_back(gap(before));body.push_back(std::move(item));};
    auto title=[&](const std::string& name,const char* back,const Items& trailing=Items{}){
        Items cols{icon_button(icons,"back",back,"Back",true,compact?18:24),pad(6),col({text(name,compact?"Small":"Default",false,true)},"stretch",compact?18:24)};
        for(auto& item:trailing)cols.push_back(item);add(columns(cols,compact?18:24));
    };
    auto page_controls=[&](const char* prefix,int page,int pages){
        return Items{col({text(std::to_string(page+1)+" / "+std::to_string(pages),"Small",true)},"auto"),pad(4),
            icon_button(icons,"previous",std::string(prefix)+"previous","Previous page",page>0,compact?18:24),pad(2),icon_button(icons,"next",std::string(prefix)+"next","Next page",page+1<pages,compact?18:24)};
    };
    auto preset_label=s.eq_supported?std::string(preset_name(s.preset)):"Not checked";
    auto eq_entry=[&](bool preview){
        auto entry=columns({col({glyph(icons,"equalizer",20)},"22px",38),pad(8),col({text("Equalizer","Default",false,true),text(preset_label,"Small",true)},"stretch",38),col({glyph(icons,"next",16)},"20px",38)},38);
        Items contents{image(icons.divider,268,1),gap(expanded?8:6),box({entry},38,action("view.eq","Open equalizer"))};
        if(preview&&curve_known&&!icons.curve_light.empty()){
            contents.push_back(gap(14));contents.push_back(image(curve_art(icons,"summary"),268,88,"Equalizer curve. Open Equalizer to edit."));contents.push_back(gap(4));
            contents.push_back(columns({col({text("Clear Bass","Small",true)},"stretch"),col({text(signed_value(s.bass),"Small",false,true,"Right")},"48px")},18));
        }else if(preview){contents.push_back(gap(14));contents.push_back(text(s.eq_supported&&s.preset==MDR_EQ_OFF?"Equalizer is off":"Curve not reported","Small",true));}
        return box(contents);
    };
    auto noise_tiles=[&](){
        Items tiles;const int modes[]={1,2,0};const char* glyphs[]={"noise-cancelling","ambient","off"};const char* verbs[]={"noise.anc","noise.ambient","noise.off"};
        int height=compact?46:62;
        for(int i=0;i<3;++i){
            bool selected=s.fresh&&s.noise==modes[i],available=enabled&&(i==1?s.ambient_supported:s.noise_supported);if(i)tiles.push_back(pad(6));
            auto symbol=std::string(glyphs[i])+(selected?"-selected":"");auto label=i==1?"Ambient":mode_name(modes[i]);
            // Separate lines prevent the native host from keeping this caption
            // on one line and overflowing the fixed-width mode button.
            Items caption=i==0?Items{text("Noise","Small",!available,selected,"Center",1),text("cancelling","Small",!available,selected,"Center",1)}:Items{text(label,"Small",!available,selected,"Center")};
            auto inner=box({glyph(icons,symbol.c_str(),compact?14:18),gap(compact?0:2),box(caption,compact?26:30)},height-3);
            auto tile=flat({inner},height,action(verbs[i],std::string(mode_name(modes[i]))+(selected?", selected":""),available),icons,"Container",selected);
            tiles.push_back(col({tile}));
        }return columns(tiles,height);
    };
    if(page==WidgetPage::Home){
        const char* battery_names[]={"Left","Right","Case"};Items cells;
        for(int i=0;i<3;++i){
            int value=s.batteries[i];bool valid=value>=0&&value<=100,low=battery_low(value);auto reading=valid?(i<2&&value==0?std::string("Off"):std::to_string(value)+"%"):"--";
            if(i)cells.push_back(pad(compact?10:14));
            std::string readout;auto art=icons.battery_readouts.find(compact?"small":expanded?"large":"medium");
            if(low&&art!=icons.battery_readouts.end())readout=image(art->second[i],80,compact?16:expanded?25:21,std::string(battery_names[i])+" battery "+reading+", low");
            if(readout.empty())readout=text((compact?std::string(battery_names[i])+" ":"")+reading,compact?"Small":expanded?"Large":"Medium",!s.fresh,true,"Left",1,low?"Attention":"Default");
            if(compact)cells.push_back(col({readout},"stretch",16));
            else cells.push_back(col({box({text(battery_names[i],"Small",true)},14),gap(4),box({readout},expanded?25:21),gap(5),image(icons.batteries[i],80,3,std::string(battery_names[i])+" battery "+(valid?std::to_string(value)+"%":"not reported"))},"stretch"));
        }
        auto status=s.pending?(s.status=="Connecting..."?"Connecting…":"Updating…"):!s.fresh?checked_time(s.checked):s.ready?"● Connected":"Earbuds aren’t reachable";
        if(s.pending&&!s.fresh&&s.checked)status+=" · Last seen";
        auto status_row=[&](){Items fields{col({text(status,"Small",!s.fresh)},"stretch",16)};if(!s.ready&&!s.pending)fields.push_back(col({text("Retry","Small",false,true)},"auto",16,action("retry","Retry connection")));return columns(fields,16);};
        if(compact){if(s.fresh&&!s.pending)add(columns(cells,16));else add(status_row());add(noise_tiles(),4);}
        else{
            add(status_row());add(columns(cells),expanded?8:4);add(noise_tiles(),expanded?12:6);
            if(s.noise==2&&s.ambient_supported){auto value=s.ambient>=1?std::to_string(s.ambient):"--";
                add(columns({col({text("Ambient level","Small",true)},"stretch"),icon_button(icons,"minus","ambient.down","Decrease ambient level",enabled&&s.ambient>1,28),pad(6),col({text(value,"Small",false,true,"Center")},"30px"),pad(6),icon_button(icons,"plus","ambient.up","Increase ambient level",enabled&&s.ambient<20,28)},28),expanded?8:6);
            }else add(gap(28),expanded?8:6);
            add(eq_entry(expanded),expanded?14:6);
        }
    }else if(page==WidgetPage::Equalizer){
        bool can_edit=enabled&&editable_eq(s),reset=can_edit&&view.baseline&&view.baseline->preset==s.preset&&(view.baseline->bands!=s.bands||view.baseline->bass!=s.bass);
        Items trailing;
        if(!compact){if(!s.ready||view.failed_write)trailing.push_back(col({text("Retry","Small",false,true)},"auto",24,action("retry","Refresh settings",!s.pending)));else trailing.push_back(col({text("Reset","Small",!reset)},"auto",24,action("eq.reset","Restore values captured when editing began",reset)));}
        title("Equalizer","view.home",trailing);
        // Explicit insets avoid a nested minHeight box whose native hover is
        // taller than its top-aligned contents.
        int preset_inset=expanded?8:6;
        add(box({gap(preset_inset),columns({pad(8),col({glyph(icons,"equalizer",20)},"20px",20),pad(8),col({text(preset_label,"Default")},"stretch",20),col({glyph(icons,"next",16)},"20px",20),pad(8)},20),gap(preset_inset)},0,action("view.presets","Choose equalizer preset")),6);
        if(curve_known){
            if(!icons.curve_light.empty())add(image(curve_art(icons,expanded?"large":"medium"),268,expanded?88:44,"Equalizer curve from −10 to +10. Values are listed below."),expanded?8:6);
            if(!expanded){Items bands;for(int i=0;i<5;++i){bool selected=i==band;auto choice=box({box({text(short_names[i],"Small",false,selected,"Center",1)},22),selected?image(icons.marker,48,2):gap(2)},24,action("view.band."+std::to_string(i),std::string("Select ")+names[i]+(selected?", selected":"")));bands.push_back(col({choice}));}add(columns(bands,24));}
            auto displayed=[&](int index){int confirmed=index==5?s.bass:s.bands[index];std::string result=signed_value(confirmed);
                if(view.proposed&&s.pending){auto& p=*view.proposed;std::optional<int> wanted;if(index==5&&(p.operation==Operation::Bass||p.operation==Operation::Reset))wanted=p.operation==Operation::Bass?p.value:p.bass;else if(index<5&&(p.operation==Operation::Bands||p.operation==Operation::Reset)&&p.bands.size()==5)wanted=p.bands[index];if(wanted&&*wanted!=confirmed)result+=" → "+signed_value(*wanted)+" …";}
                return result;
            };
            auto step_row=[&](int index,int height){int value=index==5?s.bass:s.bands[index];
                return columns({col({text(names[index],"Small",!s.fresh)},"stretch"),col({text(displayed(index),"Small",!s.fresh,true,"Center")},"52px"),pad(8),icon_button(icons,"minus","eq.decrease."+std::to_string(index),std::string("Decrease ")+names[index],can_edit&&value>-10,28),pad(6),icon_button(icons,"plus","eq.increase."+std::to_string(index),std::string("Increase ")+names[index],can_edit&&value<10,28)},height);
            };
            if(expanded){for(int i=0;i<5;++i)add(step_row(i,28),i?2:8);}else add(step_row(band,30),4);
            add(step_row(5,expanded?32:30),expanded?8:4);
            auto note=view.failed_write?"Change not confirmed. Retry above.":s.pending?"Waiting for earbuds…":!s.fresh?checked_time(s.checked):!editable_eq(s)?"Choose a custom preset to edit.":"";
            if(!note.empty())add(text(note,"Small",true),expanded?8:4);
        }else add(text(s.eq_supported?"Choose a preset to view its curve.":"EQ has not been reported.","Small",true),compact?4:12);
    }else if(page==WidgetPage::Presets){
        auto choices=s.presets;
        std::stable_sort(choices.begin(),choices.end(),[](auto& a,auto& b){auto rank=[](uint32_t id){return id==MDR_EQ_USER_2?0:id==MDR_EQ_USER_1?1:2;};return rank(a.first)<rank(b.first);});
        const int per_page=compact?2:expanded?12:6,pages=std::max(1,(static_cast<int>(choices.size())+per_page-1)/per_page),page=std::clamp(view.preset_page,0,pages-1);
        title("Presets","view.eq",pages>1?page_controls("view.presets.",page,pages):Items{});
        for(int offset=0;offset<per_page&&page*per_page+offset<static_cast<int>(choices.size());offset+=2){Items row;
            for(int j=0;j<2&&page*per_page+offset+j<static_cast<int>(choices.size());++j){auto [id,name]=choices[page*per_page+offset+j];bool selected=s.fresh&&s.preset==id;
                if(j)row.push_back(pad(8));
                auto contents=Items{columns({pad(8),col({text(name,"Small",!enabled,selected)},"stretch",40),pad(8)},40)};
                auto tile=flat(contents,44,action("eq.preset."+std::to_string(id),name+(selected?", selected":""),enabled&&s.eq_supported),icons,"Container",selected);row.push_back(col({tile}));
            }add(columns(row,44),offset?8:compact?0:expanded?6:8);
        }
        if(choices.empty())add(text("No presets reported.","Small",true),8);
    }else if(page==WidgetPage::More){
        int page=std::clamp(view.details_page,0,3);title("Preferences","view.home",compact?page_controls("view.details.",page,4):Items{});
        const char* labels[]={"Keep connection ready","Bluetooth settings","Diagnostic logging","Device details"};
        const char* hints[]={"While connected to Windows.","Pairing and audio devices","Off unless you need it.","Connection and controls"};
        const char* glyphs[]={"link","bluetooth","log","diagnostics"};
        const char* verbs[]={"pref.release","open.bluetooth","pref.logging","view.diagnostics"};
        for(int i=compact?page:0;i<(compact?page+1:4);++i){bool toggle=i==0||i==2,on=i==0?!s.release_when_hidden:logging;
            auto trailing=toggle?text(on?"On":"Off","Small",false,true,"Center",1,on?"Accent":"Default"):glyph(icons,"next",16);
            auto hint=i==0&&compact?"While connected.":hints[i];
            auto copy=col({text(labels[i],"Small",false,true),text(hint,"Small",true)},"stretch");
            // One flat full-row target includes its square state indicator.
            auto end=col({trailing},toggle?"36px":"20px",28);
            auto row=columns({pad(8),col({glyph(icons,glyphs[i],18)},"18px"),pad(8),copy,pad(6),end,pad(8)},0);
            add(box({row},compact?42:expanded?56:46,action(verbs[i],std::string(labels[i])+(toggle?(on?", on":", off"):"")),""),compact?6:i?expanded?8:0:8);
        }
    }else{
        Items values; // Diagnostic strings deliberately live only in this view.
        std::vector<std::pair<std::string,std::string>> rows={{"Model","WF-C710N"},{"Firmware",s.firmware.empty()?"Not reported":s.firmware},{"Connection",s.pending?"Updating…":s.ready?"Ready":"Unavailable"},{"Noise control",s.noise_supported?"Supported":"Not reported"},{"Custom EQ",s.eq_supported?"5 bands + bass":"Not reported"},{"Connection policy",s.release_when_hidden?"Release hidden":"Keep ready"}};
        if(!s.error.empty())rows.emplace_back("Diagnostic",s.error);
        int count=compact?1:expanded?6:4,pages=(static_cast<int>(rows.size())+count-1)/count,page=std::clamp(view.details_page,0,pages-1);
        title("Device","view.more",pages>1?page_controls("view.details.",page,pages):Items{});
        for(int i=page*count;i<std::min(static_cast<int>(rows.size()),(page+1)*count);++i){auto& [label,value]=rows[i];
            add(box({columns({col({text(label,"Small",true)}),pad(6),col({text(value,"Small",false,false,"Right")})})},compact?42:expanded?52:46,"",""),i==page*count?compact?6:expanded?6:4:expanded?8:4);
        }
    }
    return "{\"type\":\"AdaptiveCard\",\"version\":\"1.5\",\"$schema\":\"http://adaptivecards.io/schemas/adaptive-card.json\",\"body\":"+list(body)+"}";
}


