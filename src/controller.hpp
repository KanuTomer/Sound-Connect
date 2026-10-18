#pragma once
#include "device.hpp"
#include <array>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <algorithm>

struct Snapshot {
    bool ready{}, pending{}, noise_supported{}, ambient_supported{}, eq_supported{}, fresh{};
    std::string status="Not connected", error, firmware, address;
    std::array<int,3> batteries{-1,-1,-1};
    int noise=-1, ambient=-1, voice=-1, bass{};
    uint32_t preset{};
    std::vector<int32_t> bands;
    std::vector<std::pair<uint32_t,std::string>> presets;
    int64_t checked{};
    bool release_when_hidden{};
    uint32_t connection_generation{};
};

inline const char* preset_name(uint32_t preset) {
    switch(preset) {
    case MDR_EQ_OFF:return "Off"; case MDR_EQ_BRIGHT:return "Bright"; case MDR_EQ_EXCITED:return "Excited";
    case MDR_EQ_MELLOW:return "Mellow"; case MDR_EQ_RELAXED:return "Relaxed"; case MDR_EQ_VOCAL:return "Vocal";
    case MDR_EQ_TREBLE:return "Treble boost"; case MDR_EQ_BASS:return "Bass boost"; case MDR_EQ_SPEECH:return "Speech";
    case MDR_EQ_CUSTOM:return "Manual"; case MDR_EQ_USER_1:return "Custom 1"; case MDR_EQ_USER_2:return "Custom 2";
    default:return "Device preset";
    }
}

enum class Operation { Activate, Deactivate, Retry, Noise, Ambient, Voice, Preset, Bands, Bass, Reset, Policy, Suspend, Reconnect, Disconnect };
struct Command { Operation operation; int value{}; std::vector<int32_t> bands; int bass{}; uint32_t preset{}; };

class Controller {
public:
    explicit Controller(std::function<void()> notify, Snapshot initial={}) : state_(std::move(initial)), notify_(std::move(notify)), worker_([this]{run();}) {}
    ~Controller() { {std::lock_guard lock(mutex_); stopping_=true;} SetEvent(wake_); worker_.join();CloseHandle(wake_); }
    Snapshot snapshot() const { std::lock_guard lock(mutex_); return state_; }
    void interest(bool active) {
        std::lock_guard lock(mutex_);
        if (requested_active_==active) return;
        requested_active_=active;
        commands_.push_back({active?Operation::Activate:Operation::Deactivate});
        SetEvent(wake_);
    }
    void lifecycle(Operation operation){std::lock_guard lock(mutex_);if(std::none_of(commands_.begin(),commands_.end(),[&](auto& c){return c.operation==operation;}))commands_.push_back({operation});SetEvent(wake_);}
    void release_when_hidden(bool release){
        {std::lock_guard lock(mutex_);state_.release_when_hidden=release;commands_.push_back({Operation::Policy});SetEvent(wake_);}
        notify_();
    }
    bool send(Command command) {
        {
            std::lock_guard lock(mutex_);
            if(command.operation==Operation::Retry && (!requested_active_||state_.pending)) return false;
            if(command.operation!=Operation::Retry && (!requested_active_ || !state_.ready || !state_.fresh||state_.pending)) return false;
            if(command.operation==Operation::Bands || command.operation==Operation::Bass || command.operation==Operation::Ambient)
                std::erase_if(commands_,[&](const auto& c){return c.operation==command.operation;});
            if(commands_.size()>=16) return false;
            commands_.push_back(std::move(command));
            state_.pending=true;
        }
        notify_();
        SetEvent(wake_);return true;
    }
private:
    mutable std::mutex mutex_;
    static HANDLE make_wake(){auto event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!event)winrt::throw_last_error();return event;}
    HANDLE wake_=make_wake();
    Snapshot state_;
    std::function<void()> notify_;
    std::deque<Command> commands_;
    bool requested_active_{}, stopping_{};
    std::thread worker_;

    void publish(const std::function<void(Snapshot&)>& change) {
        {std::lock_guard lock(mutex_); change(state_);}
        notify_();
    }
    void read(Session& session) {
        auto next=snapshot();
        next.ready=true; next.fresh=true; next.error.clear(); next.status="Connected";
        next.firmware=session.text(MDR_TEXT_FIRMWARE_VERSION);
        next.noise_supported=session.noise_reported && session.feature(MDR_FEATURE_NOISE_CANCELLING)==MDR_AVAILABILITY_AVAILABLE;
        next.ambient_supported=session.noise_reported && session.feature(MDR_FEATURE_AMBIENT_SOUND)==MDR_AVAILABILITY_AVAILABLE;
        next.eq_supported=session.eq_reported && session.feature(MDR_FEATURE_EQUALIZER)==MDR_AVAILABILITY_AVAILABLE;
        if(session.battery_reported) {
            MDRBattery batteries[4]{}; uint32_t count=4;
            if(mdrHeadphonesGetBatteries(session.headphones,batteries,&count)==MDR_RESULT_OK) {
                next.batteries.fill(-1);
                for(uint32_t i=0;i<count;++i) {
                    int slot=batteries[i].part==MDR_BATTERY_LEFT?0:batteries[i].part==MDR_BATTERY_RIGHT?1:batteries[i].part==MDR_BATTERY_CASE?2:-1;
                    if(slot>=0 && batteries[i].present && batteries[i].level_percent<=100) next.batteries[slot]=static_cast<int>(batteries[i].level_percent);
                }
            }
        }
        MDRNoiseControl noise{};
        if(session.noise_reported && mdrHeadphonesGetNoiseControl(session.headphones,MDR_PROPERTY_CURRENT,&noise)==MDR_RESULT_OK) {
            next.noise=static_cast<int>(noise.mode); next.ambient=static_cast<int>(noise.ambient_level); next.voice=noise.focus_on_voice?1:0;
        }
        if(next.eq_supported) {
            MDREqualizer eq{};
            check(mdrHeadphonesGetEqualizer(session.headphones,MDR_PROPERTY_CURRENT,&eq),"Read EQ");
            next.preset=eq.preset; next.bass=eq.clear_bass;
            int32_t bands[10]{};uint32_t count=10;
            check(mdrHeadphonesGetEqualizerBands(session.headphones,MDR_PROPERTY_CURRENT,bands,&count),"Read EQ bands");
            next.bands.assign(bands,bands+count);
            MDREqualizerPreset presets[64]{};count=64;next.presets.clear();
            if(mdrHeadphonesGetEqualizerPresets(session.headphones,presets,&count)==MDR_RESULT_OK)
                for(uint32_t i=0;i<count;++i) {
                    if(presets[i]==MDR_EQ_UNKNOWN) continue;
                    auto name=session.text(MDR_TEXT_EQUALIZER_PRESET_NAME,i);
                    next.presets.emplace_back(presets[i],name.empty()?preset_name(presets[i]):name);
                }
        }
        next.checked=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        publish([&](Snapshot& state){next.pending=state.pending;state=std::move(next);});
    }
    void commit(Session& session,const std::function<bool()>& confirmed) {
        session.commit_confirmed(confirmed);
    }
    void execute(Session& session,const Command& command) {
        auto current=snapshot();
        if(command.operation==Operation::Noise || command.operation==Operation::Ambient || command.operation==Operation::Voice) {
            if(!current.noise_supported || (command.operation!=Operation::Noise && !current.ambient_supported)) throw std::runtime_error("This noise control is unavailable.");
            MDRNoiseControl wanted{};
            check(mdrHeadphonesGetNoiseControl(session.headphones,MDR_PROPERTY_CURRENT,&wanted),"Read noise state");
            if(command.operation==Operation::Noise) {
                if(command.value<0 || command.value>2) throw std::runtime_error("Invalid noise mode.");
                wanted.mode=static_cast<MDRNoiseMode>(command.value);
            } else if(command.operation==Operation::Voice) {
                wanted.focus_on_voice=command.value?MDR_TRUE:MDR_FALSE;
            } else {
                if(command.value<1 || command.value>20) throw std::runtime_error("Ambient level must be between 1 and 20.");
                wanted.mode=MDR_NOISE_MODE_AMBIENT; wanted.ambient_level=command.value;
            }
            session.write_noise(wanted);
            return;
        }
        if(!current.eq_supported) throw std::runtime_error("EQ is unavailable.");
        if(command.operation!=Operation::Preset&&current.preset!=MDR_EQ_CUSTOM&&current.preset!=MDR_EQ_USER_1&&current.preset!=MDR_EQ_USER_2)throw std::runtime_error("Select Manual, Custom 1, or Custom 2 before editing.");
        if((command.operation==Operation::Bass&&(command.value < -10||command.value > 10))||std::any_of(command.bands.begin(),command.bands.end(),[](int value){return value < -10||value > 10;}))throw std::runtime_error("EQ values must be between -10 and +10.");
        if(command.operation==Operation::Preset) {
            if(std::none_of(current.presets.begin(),current.presets.end(),[&](const auto& p){return p.first==static_cast<uint32_t>(command.value);})) throw std::runtime_error("Preset is not advertised by the earbuds.");
            check(mdrHeadphonesSetEqualizerPreset(session.headphones,static_cast<uint32_t>(command.value)),"Set EQ preset");
        } else if(command.operation==Operation::Bass) {
            if(current.bands.size()!=5) throw std::runtime_error("Clear Bass is unavailable for this EQ layout.");
            check(mdrHeadphonesSetEqualizerClearBass(session.headphones,command.value),"Set Clear Bass");
        } else if(command.operation==Operation::Bands || command.operation==Operation::Reset) {
            if(command.bands.size()!=current.bands.size()) throw std::runtime_error("EQ band count changed; refresh before editing.");
            if(command.operation==Operation::Reset) {
                check(mdrHeadphonesSetEqualizerPreset(session.headphones,command.preset),"Restore EQ preset");
                check(mdrHeadphonesSetEqualizerClearBass(session.headphones,command.bass),"Restore Clear Bass");
            }
            check(mdrHeadphonesSetEqualizerBands(session.headphones,command.bands.data(),static_cast<uint32_t>(command.bands.size())),"Set EQ bands");
        } else throw std::runtime_error("Unsupported setting command.");
        commit(session,[&] {
            MDREqualizer eq{};
            if(mdrHeadphonesGetEqualizer(session.headphones,MDR_PROPERTY_CURRENT,&eq)!=MDR_RESULT_OK) return false;
            if(command.operation==Operation::Preset) return eq.preset==static_cast<uint32_t>(command.value);
            if(command.operation==Operation::Bass) return eq.clear_bass==command.value;
            int32_t bands[10]{};uint32_t count=10;
            if(mdrHeadphonesGetEqualizerBands(session.headphones,MDR_PROPERTY_CURRENT,bands,&count)!=MDR_RESULT_OK) return false;
            return std::vector<int32_t>(bands,bands+count)==command.bands && (command.operation!=Operation::Reset || (eq.preset==command.preset && eq.clear_bass==command.bass));
        });
    }
    void run() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        std::unique_ptr<Session> session;
        bool active=false,suspended=false;
        auto release_at=Clock::time_point::max();
        // Earbuds often accept the Bluetooth link before their control channel
        // is ready, so a failed connect retries on its own with backoff.
        constexpr std::chrono::seconds retry_delays[]={2s,5s,10s,20s,30s,60s};
        auto retry_at=Clock::time_point::max();size_t retries=0;
        for(;;) {
            std::optional<Command> command;
            {
                std::unique_lock lock(mutex_);
                if(stopping_) break;
                if(!commands_.empty()){command=std::move(commands_.front());commands_.pop_front();}
                if(commands_.empty())ResetEvent(wake_);
            }
            bool scheduled=false,connecting=false;
            if(!command&&!session&&Clock::now()>=retry_at){scheduled=true;retry_at=Clock::time_point::max();command=Command{active?Operation::Retry:Operation::Reconnect};}
            // A fresh trigger (activation, Bluetooth link, resume) restarts the backoff.
            if(command&&!scheduled&&(command->operation==Operation::Activate||command->operation==Operation::Retry||command->operation==Operation::Reconnect)){retries=0;retry_at=Clock::time_point::max();}
            try {
                if(command && command->operation==Operation::Deactivate) {
                    active=false;release_at=snapshot().release_when_hidden?Clock::now()+15s:Clock::time_point::max();
                    if(!session) publish([](auto& s){s.ready=false;s.pending=false;s.fresh=false;s.status="Not connected";});
                } else if(command&&command->operation==Operation::Policy){release_at=!active&&snapshot().release_when_hidden?Clock::now()+15s:Clock::time_point::max();
                } else if(command&&(command->operation==Operation::Suspend||command->operation==Operation::Disconnect)){
                    suspended=command->operation==Operation::Suspend;session.reset();release_at=Clock::time_point::max();retry_at=Clock::time_point::max();retries=0;
                    publish([&](auto& s){s.ready=s.pending=s.fresh=false;s.status=suspended?"Sleeping":"Not connected";s.error.clear();});
                } else if(command && (command->operation==Operation::Activate || command->operation==Operation::Retry||command->operation==Operation::Reconnect)) {
                    if(command->operation==Operation::Reconnect){suspended=false;if(snapshot().address.empty()||(!active&&snapshot().release_when_hidden))continue;}
                    else {active=true;release_at=Clock::time_point::max();}
                    if(suspended)continue;
                    if(!session) {
                        connecting=true;session.reset();
                        publish([](auto& s){s.ready=false;s.fresh=false;s.pending=true;s.status="Connecting...";s.error.clear();});
                        auto device=find_earbuds();
                        auto previous=snapshot().address;
                        if(!previous.empty() && previous!=device.address) throw std::runtime_error("The remembered earbuds are unavailable; another WF-C710N will not be selected automatically.");
                        session=std::make_unique<Session>();session->connect(device,false,false,true);
                        publish([&](auto& s){s.address=device.address;++s.connection_generation;});
                        read(*session);retries=0;
                    }else if(command->operation!=Operation::Reconnect){
                        publish([](auto& s){s.pending=true;s.status="Refreshing...";});
                        check(mdrHeadphonesRequestSync(session->headphones),"Refresh state");session->until(MDR_EVENT_SYNC_COMPLETE,3s);read(*session);
                    }
                } else if(command) {
                    if(!session || !active) throw std::runtime_error("Controls are inactive. Open the widget and retry.");
                    execute(*session,*command);read(*session);
                }
                if(session && !active && Clock::now()>=release_at) {
                    session.reset();
                    publish([](auto& s){s.ready=false;s.fresh=false;s.status="Controls idle";});
                }
                if(session) {
                    for(int drained=0;drained<256;++drained){
                        auto event=session->poll();
                        if(event==MDR_EVENT_NEED_SYNC){
                            publish([](auto& s){s.fresh=false;});
                            check(mdrHeadphonesRequestSync(session->headphones),"Refresh changed state");
                            session->until(MDR_EVENT_SYNC_COMPLETE,3s);read(*session);
                        }else if(event==MDR_EVENT_NOISE_CONTROL_CHANGED||event==MDR_EVENT_EQUALIZER_CHANGED||event==MDR_EVENT_BATTERY_CHANGED)read(*session);
                        if(event==MDR_EVENT_NONE)break;
                        if(drained==255)throw std::runtime_error("Too many device events; controls paused to avoid continuous background work.");
                    }
                }
                bool pending;
                {std::lock_guard lock(mutex_);pending=!commands_.empty();}
                if(snapshot().pending!=pending) publish([&](auto& s){s.pending=pending;});
            } catch(const std::exception& error) {
                session.reset();
                {std::lock_guard lock(mutex_);std::erase_if(commands_,[](const auto& c){return c.operation!=Operation::Activate && c.operation!=Operation::Deactivate;});}
                publish([&](auto& s){s.ready=false;s.pending=false;s.fresh=false;s.status="Controls unavailable";s.error=error.what();});
                if(connecting&&!suspended&&retries<std::size(retry_delays))retry_at=Clock::now()+retry_delays[retries++];
            } catch(const winrt::hresult_error& error) {
                session.reset();
                if(connecting&&!suspended&&retries<std::size(retry_delays))retry_at=Clock::now()+retry_delays[retries++];
                publish([&](auto& s){s.ready=false;s.pending=false;s.fresh=false;s.status="Bluetooth unavailable";s.error=winrt::to_string(error.message());});
            }
            // No scheduled work while warm and idle. Only actual socket data,
            // commands, lifecycle events, or the optional release deadline wake us.
            HANDLE waits[]={wake_,session?session->wait_handle():nullptr};
            DWORD timeout=INFINITE;
            auto deadline=session?release_at:retry_at;
            if(deadline!=Clock::time_point::max())timeout=static_cast<DWORD>(std::clamp<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count(),0,MAXDWORD-1));
            auto waited=WaitForMultipleObjects(waits[1]?2:1,waits,FALSE,timeout);
            if(waited==WAIT_FAILED){publish([](auto& s){s.ready=s.pending=s.fresh=false;s.status="Controls unavailable";s.error="Could not wait for Bluetooth events.";});break;}
        }
    }
};
