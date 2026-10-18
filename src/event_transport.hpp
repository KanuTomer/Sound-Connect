#pragma once
#include <winsock2.h>
#include <ws2bth.h>
#include <rpc.h>
#include <mdr-c/Connection.h>
#include <string>
#include <cstdio>

// The MDR C ABI stays unchanged. Only the Windows socket wait mechanism is
// owned here, so an idle control connection can sleep until actual network I/O.
class EventClassicTransport {
public:
    MDRConnection api{};
    EventClassicTransport(){
        api.user=this;api.connect=connect;api.disconnect=disconnect;
        api.send=send;api.recv=recv;api.poll=poll;
        api.getLastError=[](void* user){return static_cast<EventClassicTransport*>(user)->error_.c_str();};
    }
    ~EventClassicTransport(){disconnect(this);if(event_!=WSA_INVALID_EVENT)WSACloseEvent(event_);if(started_)WSACleanup();}
    EventClassicTransport(const EventClassicTransport&)=delete;
    EventClassicTransport& operator=(const EventClassicTransport&)=delete;
    HANDLE event() const{return event_;}
private:
    SOCKET socket_=INVALID_SOCKET;
    WSAEVENT event_=WSA_INVALID_EVENT;
    bool started_{},connected_{};
    std::string error_;
    MDRResult fail(int code){
        char* message{};
        if(FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,reinterpret_cast<char*>(&message),0,nullptr)&&message){error_=message;LocalFree(message);while(!error_.empty()&&(error_.back()=='\r'||error_.back()=='\n'))error_.pop_back();}
        else error_="Bluetooth socket error "+std::to_string(code);
        return MDR_RESULT_ERROR_NET;
    }
    static MDRResult connect(void* user,const char* address,const char* service){
        auto& self=*static_cast<EventClassicTransport*>(user);disconnect(user);
        if(!self.started_){WSADATA data{};auto error=WSAStartup(MAKEWORD(2,2),&data);if(error)return self.fail(error);self.started_=true;}
        if(self.event_==WSA_INVALID_EVENT){self.event_=WSACreateEvent();if(self.event_==WSA_INVALID_EVENT)return self.fail(WSAGetLastError());}
        WSAResetEvent(self.event_);
        self.socket_=socket(AF_BTH,SOCK_STREAM,BTHPROTO_RFCOMM);
        if(self.socket_==INVALID_SOCKET)return self.fail(WSAGetLastError());
        if(WSAEventSelect(self.socket_,self.event_,FD_CONNECT|FD_READ|FD_WRITE|FD_CLOSE)==SOCKET_ERROR)return self.fail(WSAGetLastError());
        ULONG enable=TRUE;
        if(setsockopt(self.socket_,SOL_RFCOMM,SO_BTH_AUTHENTICATE,reinterpret_cast<char*>(&enable),sizeof(enable))==SOCKET_ERROR||setsockopt(self.socket_,SOL_RFCOMM,SO_BTH_ENCRYPT,reinterpret_cast<char*>(&enable),sizeof(enable))==SOCKET_ERROR)return self.fail(WSAGetLastError());
        SOCKADDR_BTH remote{};remote.addressFamily=AF_BTH;
        auto status=UuidFromStringA(reinterpret_cast<RPC_CSTR>(const_cast<char*>(service)),&remote.serviceClassId);if(status!=RPC_S_OK)return self.fail(status);
        unsigned int octets[6]{};
        if(sscanf_s(address,"%02x:%02x:%02x:%02x:%02x:%02x",&octets[0],&octets[1],&octets[2],&octets[3],&octets[4],&octets[5])!=6)return MDR_RESULT_ERROR_INVALID_ARGUMENT;
        for(auto octet:octets){if(octet>255)return MDR_RESULT_ERROR_INVALID_ARGUMENT;remote.btAddr=(remote.btAddr<<8)|octet;}
        if(::connect(self.socket_,reinterpret_cast<sockaddr*>(&remote),sizeof(remote))==0){self.connected_=true;return MDR_RESULT_OK;}
        auto error=WSAGetLastError();if(error==WSAEWOULDBLOCK||error==WSAEINPROGRESS)return MDR_RESULT_INPROGRESS;
        return self.fail(error);
    }
    static void disconnect(void* user){auto& self=*static_cast<EventClassicTransport*>(user);if(self.socket_!=INVALID_SOCKET){shutdown(self.socket_,SD_BOTH);closesocket(self.socket_);self.socket_=INVALID_SOCKET;}self.connected_=false;if(self.event_!=WSA_INVALID_EVENT)WSAResetEvent(self.event_);}
    static MDRResult poll(void* user,int timeout){
        auto& self=*static_cast<EventClassicTransport*>(user);if(self.socket_==INVALID_SOCKET)return MDR_RESULT_ERROR_NO_CONNECTION;
        if(!self.connected_){auto wait=WaitForSingleObject(self.event_,timeout<0?INFINITE:static_cast<DWORD>(timeout));if(wait==WAIT_TIMEOUT)return MDR_RESULT_INPROGRESS;if(wait!=WAIT_OBJECT_0)return self.fail(GetLastError());}
        WSANETWORKEVENTS events{};
        if(WSAEnumNetworkEvents(self.socket_,self.event_,&events)==SOCKET_ERROR)return self.fail(WSAGetLastError());
        for(int bit=0;bit<FD_MAX_EVENTS;++bit)if(events.iErrorCode[bit])return self.fail(events.iErrorCode[bit]);
        if(events.lNetworkEvents&FD_CLOSE){self.error_="The Bluetooth control connection closed.";return MDR_RESULT_ERROR_NO_CONNECTION;}
        if(events.lNetworkEvents&FD_CONNECT)self.connected_=true;
        return self.connected_?MDR_RESULT_OK:MDR_RESULT_INPROGRESS;
    }
    static MDRResult send(void* user,const char* bytes,int length,int* sent){auto& self=*static_cast<EventClassicTransport*>(user);*sent=::send(self.socket_,bytes,length,0);if(*sent>0)return MDR_RESULT_OK;if(*sent==0)return MDR_RESULT_ERROR_NO_CONNECTION;auto error=WSAGetLastError();*sent=0;return error==WSAEWOULDBLOCK?MDR_RESULT_INPROGRESS:self.fail(error);}
    static MDRResult recv(void* user,char* bytes,int length,int* received){auto& self=*static_cast<EventClassicTransport*>(user);*received=::recv(self.socket_,bytes,length,0);if(*received>0)return MDR_RESULT_OK;if(*received==0)return MDR_RESULT_ERROR_NO_CONNECTION;auto error=WSAGetLastError();*received=0;return error==WSAEWOULDBLOCK?MDR_RESULT_INPROGRESS:self.fail(error);}
};
