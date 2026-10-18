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
// The host's emphasis surface supplies fill, hover, focus and high-contrast
// colours natively, instead of imitating a button with PNG frames.
// The host adds no inner padding to styled containers, so content is inset
// explicitly to stay inside the tile and its hover highlight.
static std::string tile(const Items& items,int height,const std::string& click,int inset_x=12,int inset_y=8){
    Items inner{inset_x?columns({pad(inset_x),col(items),pad(inset_x)}):columns({col(items)})};
    if(inset_y){inner.insert(inner.begin(),gap(inset_y));inner.push_back(gap(inset_y));}
    return box(inner,height,click,"emphasis");
}
static std::string image_fill(const std::pair<std::string,std::string>& urls,const std::string& alt){
    if(urls.first.empty()||urls.second.empty())return {};
    return "{\"type\":\"Image\",\"spacing\":\"None\",\"url\":"+::quoted(theme(urls))+",\"size\":\"Stretch\",\"altText\":"+::quoted(alt)+"}";
}
template<class Presets> static Presets sorted_presets(Presets choices){
    std::stable_sort(choices.begin(),choices.end(),[](auto& a,auto& b){auto rank=[](uint32_t id){return id==MDR_EQ_USER_2?0:id==MDR_EQ_USER_1?1:2;};return rank(a.first)<rank(b.first);});
    return choices;
}
static std::string icon_button(const WidgetIcons& icons,const char* name,const std::string& verb,const std::string& label,bool enabled=true,int size=24){
    bool step=verb.starts_with("eq.increase.")||verb.starts_with("eq.decrease.")||verb.starts_with("ambient.");
    if(!step)return col({glyph(icons,name,18)},std::to_string(size)+"px",size,action(verb,label,enabled));
    // Columns stretch to the tallest item in their row. Keep the styled
    // click target inside the column so a taller value row cannot stretch it.
    return col({tile({glyph(icons,name,16)},size,action(verb,label,enabled),0,0)},std::to_string(size)+"px",size);
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
        Items cols{icon_button(icons,"back",back,"Back",true,compact?20:24),pad(8),col({text(name,compact?"Small":"Default",false,true)},"stretch",compact?20:24)};
        for(auto& item:trailing)cols.push_back(item);add(columns(cols,compact?20:24));
    };
    auto page_controls=[&](const char* prefix,int page,int pages){
        return Items{col({text(std::to_string(page+1)+" / "+std::to_string(pages),"Small",true)},"auto"),pad(4),
            icon_button(icons,"previous",std::string(prefix)+"previous","Previous page",page>0,compact?20:24),pad(4),icon_button(icons,"next",std::string(prefix)+"next","Next page",page+1<pages,compact?20:24)};
    };
    auto preset_label=s.eq_supported?std::string(preset_name(s.preset)):"Not checked";
    // Every Home control is the same emphasis tile, so icons sit inside one
    // visual system instead of floating on the widget background.
    auto nav_tile=[&](const char* symbol,const std::string& title,const std::string& detail,const std::string& verb,const std::string& label,int height){
        Items copy{text(title,"Default",false,true,"Left",1)};if(!detail.empty())copy.push_back(text(detail,"Small",true,false,"Left",1));
        return tile({columns({col({glyph(icons,symbol,20)},"20px"),pad(12),col(copy),col({glyph(icons,"next",16)},"16px")})},height,action(verb,label));
    };
    auto noise_tiles=[&](){
        Items tiles;const int modes[]={1,2,0};const char* glyphs[]={"noise-cancelling","ambient","off"};const char* verbs[]={"noise.anc","noise.ambient","noise.off"};
        // Small has room for icons only; the other sizes label every mode.
        int height=compact?36:76;
        for(int i=0;i<3;++i){
            bool selected=s.fresh&&s.noise==modes[i],available=enabled&&(i==1?s.ambient_supported:s.noise_supported);if(i)tiles.push_back(pad(4));
            auto symbol=std::string(glyphs[i])+(selected?"-selected":"");auto label=i==1?"Ambient":mode_name(modes[i]);
            const char* tone=selected?"Accent":"Default";
            // Separate lines prevent the native host from keeping this caption
            // on one line and overflowing the fixed-width mode button.
            Items caption=i==0?Items{text("Noise","Small",!available,selected,"Center",1,tone),text("cancelling","Small",!available,selected,"Center",1,tone)}:Items{text(label,"Small",!available,selected,"Center",2,tone)};
            Items inner=compact?Items{glyph(icons,symbol.c_str(),18)}:Items{glyph(icons,symbol.c_str(),24),gap(4),box(caption,32)};
            tiles.push_back(col({tile(inner,height,action(verbs[i],std::string(mode_name(modes[i]))+(selected?", selected":""),available),4,8)}));
        }return columns(tiles,height);
    };
    if(page==WidgetPage::Home){
        const char* battery_names[]={"Left","Right","Case"};Items cells;
        for(int i=0;i<3;++i){
            // A sleeping earbud reports 0%; show it calmly instead of as an alarm.
            int value=s.batteries[i];bool valid=value>=0&&value<=100,off=i<2&&value==0,low=battery_low(value)&&!off;auto reading=valid?(off?std::string("Off"):std::to_string(value)+"%"):"--";
            if(i)cells.push_back(pad(compact?8:16));
            // Host typography scales with text size and follows high contrast.
            auto readout=text(reading,compact?"Default":expanded?"ExtraLarge":"Medium",!s.fresh||off,true,"Left",1,low?"Attention":"Default");
            auto described=std::string(battery_names[i])+(off?" earbud off":" battery "+(valid?std::to_string(value)+"%":std::string("not reported"))+(low?", low":""));
            if(compact)cells.push_back(col({box({text(battery_names[i],"Small",true)},16),readout},"stretch",36));
            else cells.push_back(col({box({text(battery_names[i],"Small",true)},16),gap(4),box({readout},expanded?36:24),gap(expanded?8:4),image(icons.batteries[i],80,3,described)},"stretch"));
        }
        // Status only earns a row when something needs attention.
        const bool healthy=s.fresh&&s.ready;
        // Reconnection is automatic, so the row only reports state.
        auto status=s.pending?(s.status=="Connecting..."?"Connecting…":"Updating…"):!s.ready?"Disconnected":"Updating…";
        auto status_row=[&](){return columns({col({text(status,"Small",true)},"stretch",16)},16);};
        if(compact){if(s.fresh)add(columns(cells,36));else add(box({status_row()},36));add(noise_tiles(),4);}
        else{
            if(!healthy)add(status_row());
            add(columns(cells),healthy?0:expanded?8:4);
            add(noise_tiles(),expanded?20:12);
            if(!expanded)add(nav_tile("equalizer","Equalizer",preset_label,"view.eq","Open equalizer",56),12);
            else{
                Items card{columns({col({glyph(icons,"equalizer",20)},"20px"),pad(12),col({text("Equalizer","Default",false,true,"Left",1),text(preset_label,"Small",true,false,"Left",1)}),col({glyph(icons,"next",16)},"16px")})};
                // The curve slot keeps one height in every state, so choosing a
                // preset never shifts the controls below it under the pointer.
                if(curve_known&&!icons.curve_light.empty()){
                    card.push_back(gap(8));card.push_back(box({image_fill(curve_art(icons,"summary"),"Equalizer curve. Open Equalizer to edit.")},84));
                    card.push_back(columns({col({text("Clear Bass","Small",true)}),col({text(signed_value(s.bass),"Small",false,true,"Right")},"48px")},20));
                }else{card.push_back(gap(8));card.push_back(box({text(s.eq_supported&&s.preset==MDR_EQ_OFF?"Equalizer is off":"Curve not reported","Small",true,false,"Center")},104));}
                add(tile(card,0,action("view.eq","Open equalizer"),12,12),20);
                // Quick presets: the user's own first, always including the current one.
                auto quick=sorted_presets(s.presets);
                if(quick.size()>6){quick.resize(6);auto current=std::find_if(s.presets.begin(),s.presets.end(),[&](auto& p){return p.first==s.preset;});
                    if(current!=s.presets.end()&&std::none_of(quick.begin(),quick.end(),[&](auto& p){return p.first==s.preset;}))quick[5]=*current;}
                for(size_t row=0;row<quick.size();row+=3){Items chips;
                    for(size_t j=row;j<std::min(quick.size(),row+3);++j){auto& [id,name]=quick[j];bool selected=s.fresh&&s.preset==id;if(j>row)chips.push_back(pad(4));
                        chips.push_back(col({tile({text(name,"Small",!enabled,selected,"Center",1,selected?"Accent":"Default")},36,action("eq.preset."+std::to_string(id),name+(selected?", selected":""),enabled&&s.eq_supported),4,8)}));}
                    while(chips.size()<5){chips.push_back(pad(4));chips.push_back(col({}));}
                    add(columns(chips,36),row?4:8);
                }
            }
        }
    }else if(page==WidgetPage::Equalizer){
        bool can_edit=enabled&&editable_eq(s),reset=can_edit&&view.baseline&&view.baseline->preset==s.preset&&(view.baseline->bands!=s.bands||view.baseline->bass!=s.bass);
        Items trailing;
        if(!compact){if(view.failed_write)trailing.push_back(col({text("Retry","Small",false,true)},"auto",24,action("retry","Refresh settings",!s.pending)));else trailing.push_back(col({text("Reset","Small",!reset)},"auto",24,action("eq.reset","Restore values captured when editing began",reset)));
            trailing.push_back(pad(12));trailing.push_back(icon_button(icons,"settings","view.more","Preferences",true,24));}
        title("Equalizer","view.home",trailing);
        // Explicit insets avoid a nested minHeight box whose native hover is
        // taller than its top-aligned contents.
        int preset_inset=expanded?8:4;
        add(box({gap(preset_inset),columns({pad(8),col({glyph(icons,"equalizer",20)},"20px",20),pad(8),col({text(preset_label,"Default")},"stretch",20),col({glyph(icons,"next",16)},"20px",20),pad(8)},20),gap(preset_inset)},0,action("view.presets","Choose equalizer preset")),expanded?8:4);
        if(curve_known){
            if(!icons.curve_light.empty())add(image(curve_art(icons,expanded?"large":"medium"),268,expanded?88:44,"Equalizer curve from −10 to +10. Values are listed below."),expanded?8:4);
            if(!expanded){Items bands;for(int i=0;i<5;++i){bool selected=i==band;auto choice=box({box({text(short_names[i],"Small",false,selected,"Center",1)},22),selected?image(icons.marker,48,2):gap(2)},24,action("view.band."+std::to_string(i),std::string("Select ")+names[i]+(selected?", selected":"")));bands.push_back(col({choice}));}add(columns(bands,24),4);}
            auto step_row=[&](int index){int value=index==5?s.bass:s.bands[index];
                return columns({col({text(names[index],"Small",!s.fresh)},"stretch"),col({text(signed_value(value),"Small",!s.fresh,true,"Center")},"52px"),pad(8),icon_button(icons,"minus","eq.decrease."+std::to_string(index),std::string("Decrease ")+names[index],can_edit&&value>-10,32),pad(8),icon_button(icons,"plus","eq.increase."+std::to_string(index),std::string("Increase ")+names[index],can_edit&&value<10,32)},32);
            };
            if(expanded){for(int i=0;i<5;++i)add(step_row(i),i?4:8);}else add(step_row(band),4);
            add(step_row(5),expanded?8:4);
            std::string note=view.failed_write?"Change not confirmed. Retry above.":s.pending?"":!s.ready?"Disconnected":!editable_eq(s)?"Choose a custom preset to edit.":"";
            if(!note.empty())add(text(note,"Small",true),expanded?8:4);
        }else add(text(s.eq_supported?"Choose a preset to view its curve.":"EQ has not been reported.","Small",true),compact?4:12);
    }else if(page==WidgetPage::Presets){
        auto choices=sorted_presets(s.presets);
        const int per_page=compact?2:expanded?12:6,pages=std::max(1,(static_cast<int>(choices.size())+per_page-1)/per_page),page=std::clamp(view.preset_page,0,pages-1);
        title("Presets","view.eq",pages>1?page_controls("view.presets.",page,pages):Items{});
        for(int offset=0;offset<per_page&&page*per_page+offset<static_cast<int>(choices.size());offset+=2){Items row;
            for(int j=0;j<2&&page*per_page+offset+j<static_cast<int>(choices.size());++j){auto [id,name]=choices[page*per_page+offset+j];bool selected=s.fresh&&s.preset==id;
                if(j)row.push_back(pad(8));
                auto label=text(name,"Small",!enabled,selected,"Center",2,selected?"Accent":"Default");
                row.push_back(col({tile({label},44,action("eq.preset."+std::to_string(id),name+(selected?", selected":""),enabled&&s.eq_supported),8,8)}));
            }add(columns(row,44),offset||!compact?8:0);
        }
        if(choices.empty())add(text("No presets reported.","Small",true),8);
    }else if(page==WidgetPage::More){
        struct Row{const char* symbol;const char* label;std::string hint;const char* verb;int kind;bool on;}; // kind: 0 link, 1 toggle, 2 stepper
        std::vector<Row> rows;
        // Ambient tuning is occasional, so it lives here rather than on Home.
        if(s.ambient_supported){
            rows.push_back({"ambient","Ambient level",s.ambient>=1?"Higher lets in more sound":"Not checked","",2,false});
            rows.push_back({"ambient","Voice passthrough","Hear voices in Ambient","ambient.voice",1,s.voice==1});
        }
        rows.push_back({"link","Keep connection ready",compact?"While connected.":"While connected to Windows.","pref.release",1,!s.release_when_hidden});
        rows.push_back({"bluetooth","Bluetooth settings","Pairing and audio devices","open.bluetooth",0,false});
        rows.push_back({"log","Diagnostic logging","Off unless you need it.","pref.logging",1,logging});
        rows.push_back({"diagnostics","Device details","Connection and controls","view.diagnostics",0,false});
        const int per_page=compact?1:expanded?6:3,pages=(static_cast<int>(rows.size())+per_page-1)/per_page,page=std::clamp(view.details_page,0,pages-1);
        title("Preferences","view.eq",pages>1?page_controls("view.details.",page,pages):Items{});
        for(int i=page*per_page;i<std::min(static_cast<int>(rows.size()),(page+1)*per_page);++i){auto& r=rows[i];
            auto copy=col({text(r.label,"Small",false,true),text(r.hint,"Small",true)},"stretch");
            Items row{pad(8),col({glyph(icons,r.symbol,18)},"18px"),pad(8),copy,pad(8)};
            std::string click;
            if(r.kind==2){auto value=s.ambient>=1?std::to_string(s.ambient):"--";
                for(auto& item:Items{icon_button(icons,"minus","ambient.down","Decrease ambient level",enabled&&s.ambient>1,32),pad(4),col({text(value,"Small",false,true,"Center")},"28px",32),pad(4),icon_button(icons,"plus","ambient.up","Increase ambient level",enabled&&s.ambient<20,32)})row.push_back(item);
            }else{
                // One flat full-row target includes its state or chevron.
                row.push_back(col({r.kind==1?text(r.on?"On":"Off","Small",false,true,"Center",1,r.on?"Accent":"Default"):glyph(icons,"next",16)},r.kind==1?"36px":"20px",28));
                bool available=std::string_view(r.verb)!="ambient.voice"||enabled;
                click=action(r.verb,std::string(r.label)+(r.kind==1?(r.on?", on":", off"):""),available);
            }
            row.push_back(pad(8));
            add(box({columns(row,0)},compact?44:expanded?56:44,click,""),i==page*per_page?8:expanded?8:4);
        }
    }else{
        Items values; // Diagnostic strings deliberately live only in this view.
        std::vector<std::pair<std::string,std::string>> rows={{"Model","WF-C710N"},{"Firmware",s.firmware.empty()?"Not reported":s.firmware},{"Connection",s.pending?"Updating…":s.ready?"Ready":"Unavailable"},{"Noise control",s.noise_supported?"Supported":"Not reported"},{"Custom EQ",s.eq_supported?"5 bands + bass":"Not reported"},{"Connection policy",s.release_when_hidden?"Release hidden":"Keep ready"}};
        if(!s.error.empty())rows.emplace_back("Diagnostic",s.error);
        int count=compact?1:expanded?6:4,pages=(static_cast<int>(rows.size())+count-1)/count,page=std::clamp(view.details_page,0,pages-1);
        title("Device","view.more",pages>1?page_controls("view.details.",page,pages):Items{});
        for(int i=page*count;i<std::min(static_cast<int>(rows.size()),(page+1)*count);++i){auto& [label,value]=rows[i];
            add(box({columns({col({text(label,"Small",true)}),pad(8),col({text(value,"Small",false,false,"Right")})})},compact?44:expanded?52:44,"",""),i==page*count?compact?8:expanded?8:4:expanded?8:4);
        }
    }
    return "{\"type\":\"AdaptiveCard\",\"version\":\"1.5\",\"$schema\":\"http://adaptivecards.io/schemas/adaptive-card.json\",\"body\":"+list(body)+"}";
}


