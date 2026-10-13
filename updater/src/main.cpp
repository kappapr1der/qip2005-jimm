#include "transaction.hpp"
#include "generated_config.hpp"
#include <shellapi.h>

using namespace qip;
static Policy policy(){return {Repository,PublicModulus,QipHash,DockingHash};}
static void notice(const wchar_t* text){MessageBoxW(nullptr,text,L"QIP Update Agent",MB_OK|MB_ICONINFORMATION);}
static bool consent(const std::string& version){auto text=L"Доступно обновление QIP "+wide(version)+L".\n\nУстановщик скачан, его подпись и SHA-256 проверены.\nУстановить обновление после закрытия QIP?\nАктивная переписка прервана не будет.";return MessageBoxW(nullptr,text.c_str(),L"Обновление QIP",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)==IDYES;}
static fs::path validatedApp(const fs::path& app){noLinks(app);require(_wcsicmp(app.filename().c_str(),L"QIP2005")==0,"application_directory");return fs::absolute(app).lexically_normal();}
static void stageWorker(const fs::path& app,const Offer& offer,const fs::path& installer,bool automatic){
    auto id=uniqueId();auto jobDir=dataRoot()/L"transactions"/wide(id);noLinks(jobDir);fs::create_directories(jobDir);
    fs::copy_file(modulePath(),jobDir/L"QipUpdateWorker.exe");require(hashFile(jobDir/L"QipUpdateWorker.exe")==hashFile(modulePath()),"worker_copy");
    writeAtomic(jobDir/L"manifest.json",offer.bytes);writeAtomic(jobDir/L"manifest.sig",offer.signature);
    writeAtomic(jobDir/L"job.json",json{{"app",utf8(app.wstring())},{"installer",utf8(installer.wstring())},{"automatic",automatic},{"id",id}}.dump());
    writeAtomic(jobDir/L"phase","prepared");process(jobDir/L"QipUpdateWorker.exe",L"--worker "+quote(wide(id)),false,jobDir);
}
static bool checkCycle(const fs::path& app,bool manual){
    auto config=settings();auto state=readJson(dataRoot()/L"state.json");auto timestamp=now();
    if(!manual && !due(timestamp,state.value("last_check",0ULL),config.value("check_automatically",true)))return false;
    state["last_check"]=timestamp;writeAtomic(dataRoot()/L"state.json",state.dump());
    Http http;auto offer=checkUpdate(http,policy(),installedVersion(app),state.value("highest_authenticated_version",std::string()),timestamp);
    if(!offer){log("no_update");if(manual)notice(L"Новых стабильных обновлений нет.");return false;}
    state["highest_authenticated_version"]=offer->manifest.at("version").get<std::string>();writeAtomic(dataRoot()/L"state.json",state.dump());
    std::string version=offer->manifest.at("version").get<std::string>();
    if(!manual && (state.value("declined_version",std::string())==version || state.value("failed_version",std::string())==version))return false;
    auto cache=dataRoot()/L"downloads"/wide(version);noLinks(cache);fs::create_directories(cache);
    auto setup=cache/wide(offer->manifest.at("asset").get<std::string>());
    bool automatic=config.value("install_after_exit",false);
    if(!downloadAndApprove(http,*offer,setup,automatic,[&]{return consent(version);})){state["declined_version"]=version;writeAtomic(dataRoot()/L"state.json",state.dump());log("user_declined");return false;}
    log("update_authenticated");
    stageWorker(app,*offer,setup,automatic);log("update_scheduled");return true;
}
static void monitor(const fs::path& app,bool manual){
    Mutex single(lockName("Monitor",app),true);if(!single.acquired())return;
    do {
        try {if(checkCycle(app,manual))return;}
        catch(const std::exception& error){log("check_failed");logFailure(error);if(manual)notice(L"Проверка обновления не выполнена. Файлы QIP не изменены.\nПроверьте подключение к сети и журнал обновлятора.");}
        if(manual || !appBusy(app,GetCurrentProcessId()))return;
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }while(appBusy(app,GetCurrentProcessId()));
}
static void worker(const std::string& id){
    require(std::regex_match(id,std::regex(R"(\{[0-9A-Fa-f-]{36}\})")),"job_id");auto dir=dataRoot()/L"transactions"/wide(id);noLinks(dir);
    auto job=readJson(dir/L"job.json");require(job.at("id").get<std::string>()==id,"job_id");auto app=validatedApp(wide(job.at("app").get<std::string>()));
    Mutex lock(lockName("Install",app),true);if(!lock.acquired())return;
    auto raw=read(dir/L"manifest.json",128*1024),signature=read(dir/L"manifest.sig",2048);
    require(verifyRsa(raw,signature,PublicModulus),"signature_invalid");auto manifest=json::parse(raw);
    auto phase=read(dir/L"phase",64);auto backup=dataRoot()/L"backups"/wide(id);noLinks(backup);
    WindowsIntegration integration;WindowsInstaller installer;
    while(installer.busy(app))std::this_thread::sleep_for(std::chrono::seconds(2));
    std::this_thread::sleep_for(std::chrono::seconds(2));require(!installer.busy(app),"application_busy");
    if(phase=="installing" || phase=="restoring"){
        writeAtomic(dir/L"phase","restoring");Snapshot(backup).restore(app,integration,manifest.at("files"));writeAtomic(dir/L"phase","rolled-back");log("crash_recovered");return;
    }
    require(phase=="prepared","job_finished");manifest=authenticate(policy(),raw,signature,now());
    if(job.at("automatic").get<bool>() && !settings().value("install_after_exit",false)){writeAtomic(dir/L"phase","cancelled");log("automatic_install_disabled");return;}
    auto setup=fs::path(wide(job.at("installer").get<std::string>()));auto expected=dataRoot()/L"downloads"/wide(manifest.at("version").get<std::string>())/wide(manifest.at("asset").get<std::string>());require(samePath(setup,expected),"installer_path");noLinks(setup);
    Offer offer{manifest,raw,signature,{}};auto result=apply(installer,integration,app,setup,backup,dir/L"phase",externalHistory(),offer);
    if(result==ApplyResult::busy){writeAtomic(dir/L"phase","deferred");log("install_deferred");return;}
    if(result==ApplyResult::installed)log("update_installed");
    else{auto state=readJson(dataRoot()/L"state.json");state["failed_version"]=manifest.at("version");writeAtomic(dataRoot()/L"state.json",state.dump());log("update_rolled_back");notice(L"Обновление не установлено. Предыдущая версия QIP и пользовательские данные восстановлены. Резервная копия сохранена.");}
}
static bool recoverPending(const fs::path& app){
    auto root=dataRoot()/L"transactions";if(!fs::exists(root))return false;noLinks(root);
    for(auto& entry:fs::directory_iterator(root)){noLinks(entry.path());if(!entry.is_directory() || !fs::exists(entry.path()/L"phase"))continue;
        auto phase=read(entry.path()/L"phase",64);if(phase!="installing" && phase!="restoring")continue;
        auto job=readJson(entry.path()/L"job.json");if(!samePath(wide(job.at("app").get<std::string>()),app))continue;
        process(entry.path()/L"QipUpdateWorker.exe",L"--worker "+quote(entry.path().filename().wstring()),false,entry.path());
        notice(L"Обнаружено незавершённое обновление. Выполняется восстановление.\nПосле его завершения снова запустите QIP.");return true;
    }return false;
}
static void launch(const fs::path& app){
    if(recoverPending(app))return;
    Mutex gate(lockName("Install",app),true);if(!gate.acquired()){notice(L"Обновление ожидает закрытия QIP или уже выполняется.\nДождитесь завершения и снова запустите клиент.");return;}
    require(hashFile(app/L"qip.exe")==QipHash && hashFile(app/L"Plugins"/L"docking.dll")==DockingHash,"original_binary_changed");
    process(app/L"qip.exe",L"",false,app);
    try{process(app/L"QipUpdateAgent.exe",L"--monitor",false,app);}catch(...){log("monitor_start_failed");}
}
static HWND autoCheckBox,autoInstallBox,historyEdit;static fs::path uiApp;
static void saveUi(){auto s=settings();s["check_automatically"]=SendMessageW(autoCheckBox,BM_GETCHECK,0,0)==BST_CHECKED;s["install_after_exit"]=SendMessageW(autoInstallBox,BM_GETCHECK,0,0)==BST_CHECKED;wchar_t path[32768];GetWindowTextW(historyEdit,path,32768);s["external_history_directory"]=utf8(path);saveSettings(s);}
static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_COMMAND){try{switch(LOWORD(w)){
        case 101:saveUi();notice(L"Настройки сохранены.");return 0;
        case 102:saveUi();process(uiApp/L"QipUpdateAgent.exe",L"--check",false,uiApp);return 0;
        case 103:ShellExecuteW(window,L"open",dataRoot().c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;
        case 104:DestroyWindow(window);return 0;
    }}catch(...){notice(L"Не удалось сохранить настройки или выполнить действие. Подробности в журнале.");log("settings_failed");}}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,w,l);
}
static void settingsWindow(const fs::path& app,HINSTANCE instance){
    fs::create_directories(dataRoot());uiApp=app;WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=instance;wc.lpszClassName=L"QIPUpdateSettings";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);
    auto window=CreateWindowW(wc.lpszClassName,L"QIP 2005 — обновления",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,620,390,nullptr,nullptr,instance,nullptr);require(window,"settings_window");
    auto add=[&](const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id){auto h=CreateWindowW(type,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,(HMENU)(INT_PTR)id,instance,nullptr);SendMessageW(h,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);return h;};
    auto current=L"Установленная версия: "+wide(installedVersion(app));add(L"STATIC",current.c_str(),0,20,20,570,24,0);
    autoCheckBox=add(L"BUTTON",L"Проверять и скачивать обновления автоматически",BS_AUTOCHECKBOX|WS_TABSTOP,20,60,570,28,0);
    autoInstallBox=add(L"BUTTON",L"Устанавливать автоматически после закрытия QIP",BS_AUTOCHECKBOX|WS_TABSTOP,20,95,570,28,0);
    add(L"STATIC",L"Иначе перед установкой появится запрос. Переписка не прерывается.",0,20,130,570,24,0);
    add(L"STATIC",L"История вне Users: дополнительная папка для резервной копии",0,20,170,570,24,0);
    historyEdit=add(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,20,200,570,26,0);
    add(L"BUTTON",L"Сохранить",BS_PUSHBUTTON|WS_TABSTOP,20,245,130,32,101);
    add(L"BUTTON",L"Проверить сейчас",BS_PUSHBUTTON|WS_TABSTOP,165,245,160,32,102);
    add(L"BUTTON",L"Журнал и копии",BS_PUSHBUTTON|WS_TABSTOP,340,245,150,32,103);
    add(L"BUTTON",L"Закрыть",BS_PUSHBUTTON|WS_TABSTOP,500,245,90,32,104);
    add(L"STATIC",L"Источники: GitHub Releases. Проверка подписи обязательна.",0,20,300,570,24,0);
    auto s=settings();SendMessageW(autoCheckBox,BM_SETCHECK,s.value("check_automatically",true)?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(autoInstallBox,BM_SETCHECK,s.value("install_after_exit",false)?BST_CHECKED:BST_UNCHECKED,0);SetWindowTextW(historyEdit,wide(s.value("external_history_directory",std::string())).c_str());
    ShowWindow(window,SW_SHOW);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    bool quiet=false;
    try{
        SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        int count=0;auto raw=CommandLineToArgvW(GetCommandLineW(),&count);require(raw,"arguments");std::vector<std::wstring> args(raw,raw+count);LocalFree(raw);
        auto mode=count>1?args[1]:(modulePath().filename()==L"QipLauncher.exe"?L"--launch":L"--settings");
        quiet=mode==L"--configure" || mode==L"--backup-for-install";
        if(mode==L"--worker"){require(count==3,"arguments");worker(utf8(args[2]));return 0;}
        if(mode==L"--backup-for-install" || mode==L"--restore-backup"){
            require(count==4,"arguments");auto app=validatedApp(args[2]);std::string id=utf8(args[3]);require(std::regex_match(id,std::regex(R"(manual-[0-9A-Za-z_-]{1,80})")),"backup_id");
            require(!appBusy(app,GetCurrentProcessId()),"application_busy");auto dir=dataRoot()/L"backups"/wide(id);noLinks(dir);WindowsIntegration integration;
            if(mode==L"--backup-for-install"){Snapshot(dir).create(app,externalHistory(),integration);fs::copy_file(modulePath(),dir/L"QipRecovery.exe");log("manual_install_backup");}
            else{Snapshot(dir).restore(app,integration,json::array({{{"path","QipUpdateAgent.exe"}},{{"path","QipLauncher.exe"}},{{"path","installed-version.json"}}}));log("manual_backup_restored");}return 0;
        }
        auto app=validatedApp(modulePath().parent_path());
        if(mode==L"--configure"){require(count==4 && (args[2]==L"0"||args[2]==L"1") && (args[3]==L"0"||args[3]==L"1"),"arguments");auto s=settings();s["check_automatically"]=args[2]==L"1";s["install_after_exit"]=args[3]==L"1";saveSettings(s);return 0;}
        if(mode==L"--launch")launch(app);
        else if(mode==L"--monitor")monitor(app,false);
        else if(mode==L"--check")monitor(app,true);
        else if(mode==L"--settings")settingsWindow(app,instance);
        else throw std::runtime_error("arguments");return 0;
    }catch(const std::exception& error){log("agent_failed");logFailure(error);if(!quiet)notice(L"Действие обновлятора не выполнено. Проверьте журнал.\nДля восстановления доступны резервные копии.");return 1;}
}
