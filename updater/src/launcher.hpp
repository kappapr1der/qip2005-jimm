#pragma once
#include "native.hpp"

namespace qip {
struct ClientWindowSearch {DWORD pid;HWND window=nullptr;long long score=-1;};
inline BOOL CALLBACK findClientWindow(HWND window,LPARAM parameter){
    auto& search=*reinterpret_cast<ClientWindowSearch*>(parameter);DWORD pid=0;GetWindowThreadProcessId(window,&pid);
    if(pid!=search.pid)return TRUE;
    auto owner=GetWindow(window,GW_OWNER);
    if(owner){wchar_t ownerClass[128]{};GetClassNameW(owner,ownerClass,128);if(_wcsicmp(ownerClass,L"TApplication"))return TRUE;}
    auto style=GetWindowLongPtrW(window,GWL_STYLE),extra=GetWindowLongPtrW(window,GWL_EXSTYLE);
    if(!(style&WS_CAPTION) || (extra&WS_EX_TOOLWINDOW))return TRUE;
    wchar_t name[128]{};GetClassNameW(window,name,128);
    if(!_wcsicmp(name,L"TApplication") || !_wcsicmp(name,L"IME") || !_wcsicmp(name,L"MSCTFIME UI"))return TRUE;
    RECT bounds{};GetWindowRect(window,&bounds);
    long long score=std::max<LONG>(0,bounds.right-bounds.left)*static_cast<long long>(std::max<LONG>(0,bounds.bottom-bounds.top));
    if(IsWindowVisible(window))score+=100000000;
    wchar_t caption[64]{};int length=GetWindowTextW(window,caption,64);
    if(length>=5 && length<=20 && std::all_of(caption,caption+length,[](wchar_t c){return c>=L'0' && c<=L'9';}))score+=1000000000;
    if(score>search.score){search.window=window;search.score=score;}
    return TRUE;
}
inline bool showClient(DWORD pid){
    ClientWindowSearch search{pid};
    for(int retry=0;retry<40 && !search.window;++retry){EnumWindows(findClientWindow,reinterpret_cast<LPARAM>(&search));if(!search.window)Sleep(25);}
    if(!search.window)return false;
    ShowWindowAsync(search.window,IsIconic(search.window)?SW_RESTORE:SW_SHOW);
    HWND popup=GetLastActivePopup(search.window);if(!IsWindowVisible(popup))popup=search.window;
    if(!SetForegroundWindow(popup)){FLASHWINFO flash{sizeof(flash),popup,FLASHW_TRAY,3,0};FlashWindowEx(&flash);}
    return true;
}
struct ClientLaunchResult {bool started;DWORD pid;bool window_shown;};
template<class Prepare>
ClientLaunchResult startClientOnce(const fs::path& app,Prepare prepare,bool show=true){
    Mutex launch(lockName("Launch",app),true,5000);require(launch.acquired(),"launcher_busy");
    auto existing=clientProcesses(app);
    if(!existing.empty())return {false,existing.front(),show && showClient(existing.front())};
    Mutex install(lockName("Install",app),true);require(install.acquired(),"application_busy");
    existing=clientProcesses(app);if(!existing.empty())return {false,existing.front(),show && showClient(existing.front())};
    prepare();
    noLinks(app/L"qip.exe");auto executable=app/L"qip.exe";std::wstring command=quote(executable.wstring());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    require(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,app.c_str(),&startup,&child),"process_start");
    Handle process(child.hProcess),thread(child.hThread);
    return {true,child.dwProcessId,false};
}
inline ClientLaunchResult startClientOnce(const fs::path& app,bool show=true){
    return startClientOnce(app,[]{},show);
}
}
