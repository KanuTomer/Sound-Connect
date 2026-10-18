#include <windows.h>
#include <unknwn.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.AppExtensions.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.Windows.Widgets.Providers.h>
#include <iostream>

static void dump(winrt::Windows::Foundation::Collections::IPropertySet const& properties,int depth=0){
    for(auto pair:properties){
        std::wcout<<std::wstring(static_cast<size_t>(depth)*2,L' ')<<pair.Key();
        if(auto child=pair.Value().try_as<winrt::Windows::Foundation::Collections::IPropertySet>()){
            std::wcout<<L"\n";if(depth<3)dump(child,depth+1);
        }else if(auto text=pair.Value().try_as<winrt::Windows::Foundation::IPropertyValue>()){
            if(text.Type()==winrt::Windows::Foundation::PropertyType::String)std::wcout<<L" = "<<text.GetString();
            std::wcout<<L"\n";
        }else std::wcout<<L"\n";
    }
}
int main(int argc,char** argv){
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    try{
        auto catalog=winrt::Windows::ApplicationModel::AppExtensions::AppExtensionCatalog::Open(L"com.microsoft.windows.widgets");
        auto extensions=catalog.FindAllAsync().get();
        std::wcout<<L"Registered widget extensions: "<<extensions.Size()<<L"\n";
        bool ours=false;
        for(auto extension:extensions){
            auto package=std::wstring(extension.Package().Id().Name());
            if(package.find(L"Earbud")==std::wstring::npos&&package.find(L"Clipchamp")==std::wstring::npos)continue;
            std::wcout<<L"Package: "<<package<<L"; extension: "<<extension.Id()<<L"\n";
            if(package.find(L"Earbud")!=std::wstring::npos)ours=true;
            dump(extension.GetExtensionPropertiesAsync().get());
        }
        std::wcout<<L"Earbud extension registered: "<<(ours?L"yes":L"no")<<L"\n";
        if(argc<2 || std::string(argv[1])!="--activate")return ours?0:1;
        constexpr GUID clsid{0x891e4d36,0xa7cb,0x423c,{0xb1,0x50,0x96,0xd9,0xeb,0x10,0x71,0xc7}};
        winrt::com_ptr<IUnknown> provider;
        auto hr=CoCreateInstance(clsid,nullptr,CLSCTX_LOCAL_SERVER,IID_IUnknown,provider.put_void());
        std::wcout<<L"Provider activation HRESULT: 0x"<<std::hex<<static_cast<unsigned long>(hr)<<L"\n";
        return ours&&SUCCEEDED(hr)?0:1;
    }catch(const winrt::hresult_error& e){std::wcerr<<L"Inspection HRESULT: 0x"<<std::hex<<static_cast<unsigned long>(e.code())<<L" "<<e.message()<<L"\n";return 1;}
}
