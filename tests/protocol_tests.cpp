#include <mdr-c/Headphones.h>
#include <mdr/Command.hpp>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void expect(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Replay {
    mdr::MDRBuffer incoming;
    MDRConnection connection{};
    MDRHeadphones* headphones{};
    Replay(){
        connection.user=this;
        connection.poll=[](void*,int)->MDRResult{return MDR_RESULT_OK;};
        connection.send=[](void*,const char*,int size,int* sent)->MDRResult{*sent=size;return MDR_RESULT_OK;};
        connection.recv=[](void* user,char* dst,int size,int* received)->MDRResult{
            auto& bytes=static_cast<Replay*>(user)->incoming;
            if(bytes.empty()){*received=0;return MDR_RESULT_INPROGRESS;}
            *received=std::min(size,static_cast<int>(bytes.size()));
            std::memcpy(dst,bytes.data(),*received);bytes.erase(bytes.begin(),bytes.begin()+*received);return MDR_RESULT_OK;
        };
        expect(mdrHeadphonesCreate(MDR_ABI_VERSION,&connection,MDR_PROTOCOL_V2,&headphones)==MDR_RESULT_OK,"Create replay engine");
    }
    ~Replay(){mdrHeadphonesDestroy(headphones);}
    MDREvent payload(std::initializer_list<unsigned char> data){
        incoming=mdr::MDRPackCommand(mdr::MDRDataType::DATA_MDR,0,{data.begin(),data.size()});
        MDREvent event{};expect(mdrHeadphonesPoll(headphones,&event)==MDR_RESULT_OK,"Replay payload must parse");return event;
    }
};
int main(){try{
    Replay replay;
    expect(replay.payload({0x57,0x00,0xa2,6,14,11,10,11,12,13})==MDR_EVENT_EQUALIZER_CHANGED,"Full EQ response must update state");
    // Captured from WF-C710N firmware 1.0.5 on 2026-10-17. Payload: 59 00.
    replay.incoming={0x3e,0x0c,0x01,0,0,0,2,0x59,0,0x68,0x3c};
    MDREvent event{};
    expect(mdrHeadphonesPoll(replay.headphones,&event)==MDR_RESULT_OK,"Short EQ notification must not poison the connection");
    expect(event==MDR_EVENT_NEED_SYNC,"Short EQ notification must request authoritative readback");
    MDREqualizer eq{};int32_t bands[5]{};uint32_t count=5;
    expect(mdrHeadphonesGetEqualizer(replay.headphones,MDR_PROPERTY_CURRENT,&eq)==MDR_RESULT_OK&&eq.preset==26&&eq.clear_bass==4,"Notification must preserve confirmed preset and bass");
    expect(mdrHeadphonesGetEqualizerBands(replay.headphones,MDR_PROPERTY_CURRENT,bands,&count)==MDR_RESULT_OK&&std::vector<int32_t>(bands,bands+count)==std::vector<int32_t>({1,0,1,2,3}),"Notification must not invent band values");
    expect(replay.payload({0x59,0,0xa2,6,14,11,10,11,12,13})==MDR_EVENT_EQUALIZER_CHANGED,"Full EQ notifications must still parse");
    Replay malformed;
    malformed.incoming=mdr::MDRPackCommand(mdr::MDRDataType::DATA_MDR,0,std::array<unsigned char,3>{0x59,0,26});
    expect(mdrHeadphonesPoll(malformed.headphones,&event)!=MDR_RESULT_OK,"Truncated full notifications must still fail");
    std::cout<<"Passed: captured EQ invalidation, preserved values, full replies and notifications, malformed rejection.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
