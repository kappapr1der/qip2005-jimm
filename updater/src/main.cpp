#include "transaction.hpp"
#include "generated_config.hpp"
#include "profile_backup.hpp"
#include <shellapi.h>
#include <ctime>

using namespace qip;
static Policy policy(){return {Repository,PublicModulus,QipHash,DockingHash};}
#ifdef QIP_AGENT_TESTING
static int testNotices=0;
#endif
static void notice(const wchar_t* text){
#ifdef QIP_AGENT_TESTING
    (void)text;++testNotices;
#else
    MessageBoxW(nullptr,text,L"QIP Update Agent",MB_OK|MB_ICONINFORMATION);
#endif
}
static bool consent(const std::string& version){auto text=L"Доступно обновление QIP "+wide(version)+L".\n\nУстановщик скачан, его подпись и SHA-256 проверены.\nУстановить обновление после закрытия QIP?\nАктивная переписка прервана не будет.";return MessageBoxW(nullptr,text.c_str(),L"Обновление QIP",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)==IDYES;}
static fs::path validatedApp(const fs::path& app){noLinks(app);require(_wcsicmp(app.filename().c_str(),L"QIP2005")==0,"application_directory");return fs::absolute(app).lexically_normal();}
static void stageWorker(const fs::path& app,const Offer& offer,const fs::path& installer,bool automatic){
    auto id=uniqueId();auto jobDir=dataRoot()/L"transactions"/wide(id);noLinks(jobDir);fs::create_directories(jobDir);
    fs::copy_file(modulePath(),jobDir/L"QipUpdateWorker.exe");require(hashFile(jobDir/L"QipUpdateWorker.exe")==hashFile(modulePath()),"worker_copy");
    writeAtomic(jobDir/L"manifest.json",offer.bytes);writeAtomic(jobDir/L"manifest.sig",offer.signature);
    writeAtomic(jobDir/L"job.json",json{{"app",utf8(app.wstring())},{"installer",utf8(installer.wstring())},{"automatic",automatic},{"id",id}}.dump());
    writeAtomic(jobDir/L"phase","prepared");process(jobDir/L"QipUpdateWorker.exe",L"--worker "+quote(wide(id)),false,jobDir);
}
static bool checkCycle(const fs::path& app,bool manual,Transport* transport=nullptr){
    Mutex checking(lockName("Check",app),true,manual?30000:0);
    if(!checking.acquired()){if(manual)notice(L"Проверка обновлений уже выполняется. Попробуйте ещё раз после её завершения.");return false;}
    auto config=settings();auto state=readJson(dataRoot()/L"state.json");auto timestamp=now();
    if(!manual && !due(timestamp,state.value("last_check",0ULL),config.value("check_automatically",true)))return false;
    state["last_check"]=timestamp;writeAtomic(dataRoot()/L"state.json",state.dump());
    Http http;Transport& connection=transport?*transport:http;auto offer=checkUpdate(connection,policy(),installedVersion(app),state.value("highest_authenticated_version",std::string()),timestamp);
    if(!offer){log("no_update");if(manual)notice(L"Новых стабильных обновлений нет.");return false;}
    state["highest_authenticated_version"]=offer->manifest.at("version").get<std::string>();writeAtomic(dataRoot()/L"state.json",state.dump());
    std::string version=offer->manifest.at("version").get<std::string>();
    if(!manual && (state.value("declined_version",std::string())==version || state.value("failed_version",std::string())==version))return false;
    auto cache=dataRoot()/L"downloads"/wide(version);noLinks(cache);fs::create_directories(cache);
    auto setup=cache/wide(offer->manifest.at("asset").get<std::string>());
    bool automatic=config.value("install_after_exit",false);
    if(!downloadAndApprove(connection,*offer,setup,automatic,[&]{return consent(version);})){state["declined_version"]=version;writeAtomic(dataRoot()/L"state.json",state.dump());log("user_declined");return false;}
    log("update_authenticated");
    stageWorker(app,*offer,setup,automatic);log("update_scheduled");return true;
}
static void monitor(const fs::path& app,bool manual,Transport* transport=nullptr){
    Mutex single(lockName(manual?"ManualCheck":"Monitor",app),true);if(!single.acquired())return;
    do {
        try {if(checkCycle(app,manual,transport))return;}
        catch(const std::exception& error){log("check_failed");logFailure(error);if(manual)notice(L"Проверка обновления не выполнена. Файлы QIP не изменены.\nПроверьте подключение к сети и журнал обновлятора.");}
        if(manual)return;
        if(!clientRunning(app))break;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }while(clientRunning(app));
    try{auto result=ProfileBackupStore().create(app,false,settings().value("backup_after_exit",true),now(),externalHistory());if(result.status=="created")log("profile_backup_created");}
    catch(const std::exception& error){log("profile_backup_failed");logFailure(error);}
}
static void worker(const std::string& id){
    require(std::regex_match(id,std::regex(R"(\{[0-9A-Fa-f-]{36}\})")),"job_id");auto dir=dataRoot()/L"transactions"/wide(id);noLinks(dir);
    auto job=readJson(dir/L"job.json");require(job.at("id").get<std::string>()==id,"job_id");auto app=validatedApp(wide(job.at("app").get<std::string>()));
    Mutex lock(lockName("Install",app),true);if(!lock.acquired())return;
    auto raw=read(dir/L"manifest.json",128*1024),signature=read(dir/L"manifest.sig",2048);
    require(verifyRsa(raw,signature,PublicModulus),"signature_invalid");auto manifest=json::parse(raw);
    auto phase=read(dir/L"phase",64);auto backup=dataRoot()/L"backups"/wide(id);noLinks(backup);
    WindowsIntegration integration(app);WindowsInstaller installer;
    while(installer.busy(app))std::this_thread::sleep_for(std::chrono::seconds(2));
    std::this_thread::sleep_for(std::chrono::seconds(2));require(!installer.busy(app),"application_busy");
    if(phase=="installing" || phase=="restoring"){
        writeAtomic(dir/L"phase","restoring");Snapshot(backup).restore(app,integration,manifest.at("files"));writeAtomic(dir/L"phase","rolled-back");log("crash_recovered");return;
    }
    require(phase=="prepared","job_finished");manifest=authenticate(policy(),raw,signature,now());
    if(job.at("automatic").get<bool>() && !settings().value("install_after_exit",false)){writeAtomic(dir/L"phase","cancelled");log("automatic_install_disabled");return;}
    auto setup=fs::path(wide(job.at("installer").get<std::string>()));auto expected=dataRoot()/L"downloads"/wide(manifest.at("version").get<std::string>())/wide(manifest.at("asset").get<std::string>());require(samePath(setup,expected),"installer_path");noLinks(setup);
    try{ProfileBackupStore().create(app,false,settings().value("backup_after_exit",true),now(),externalHistory());}
    catch(const std::exception& error){log("profile_backup_failed");logFailure(error);}
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
    ProfileBackupStore backups;
    if(backups.pending(app) && backups.recover(app,externalHistory()))log("profile_restore_recovered");
    if(recoverPending(app))return;
    require(hashFile(app/L"qip.exe")==QipHash && hashFile(app/L"Plugins"/L"docking.dll")==DockingHash,"original_binary_changed");
    auto result=startClientOnce(app,[&]{prepareProfile(app);});
    log(result.started?"client_started":result.window_shown?"existing_client_shown":"existing_client_running");
    auto options=settings();
    if(options.value("check_automatically",true) || options.value("backup_after_exit",true)){
        try{process(app/L"QipUpdateAgent.exe",L"--monitor",false,app);}catch(...){log("monitor_start_failed");}
    }
}
static HWND autoCheckBox,autoInstallBox,backupBox,historyEdit,backupCombo;static fs::path uiApp;static std::vector<std::string> uiBackups;
static void saveUi(){auto s=settings();s["check_automatically"]=SendMessageW(autoCheckBox,BM_GETCHECK,0,0)==BST_CHECKED;s["install_after_exit"]=SendMessageW(autoInstallBox,BM_GETCHECK,0,0)==BST_CHECKED;s["backup_after_exit"]=SendMessageW(backupBox,BM_GETCHECK,0,0)==BST_CHECKED;wchar_t path[32768];GetWindowTextW(historyEdit,path,32768);if(*path){auto folder=fs::path(path);require(folder.is_absolute() && fs::is_directory(folder),"external_history_path");validateHistoryLocation(folder,uiApp,ProfileBackupStore().root());}s["external_history_directory"]=utf8(path);saveSettings(s);}
static void openFolder(const fs::path& path){noLinks(path);require(fs::is_directory(path),"folder_missing");require((INT_PTR)ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL)>32,"folder_open");}
static void refreshBackups(){
    uiBackups.clear();SendMessageW(backupCombo,CB_RESETCONTENT,0,0);
    for(auto& item:ProfileBackupStore().list(uiApp)){
        auto timestamp=(time_t)item.at("created").get<uint64_t>();std::tm local{};localtime_s(&local,&timestamp);wchar_t date[64]{};wcsftime(date,64,L"%d.%m.%Y %H:%M:%S",&local);
        auto reason=item.value("reason",std::string());std::wstring label=date;label+=reason=="manual"?L" — ручная":reason=="before-restore"?L" — до восстановления":L" — после выхода";
        SendMessageW(backupCombo,CB_ADDSTRING,0,(LPARAM)label.c_str());uiBackups.push_back(item.at("id"));
    }
    if(!uiBackups.empty())SendMessageW(backupCombo,CB_SETCURSEL,0,0);
}
static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_COMMAND){try{switch(LOWORD(w)){
        case 101:saveUi();notice(L"Настройки сохранены.");return 0;
        case 102:saveUi();process(uiApp/L"QipUpdateAgent.exe",L"--check",false,uiApp);return 0;
        case 103:ShellExecuteW(window,L"open",dataRoot().c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;
        case 104:DestroyWindow(window);return 0;
        case 105:{auto histories=configuredHistories(uiApp);auto path=histories.size()==1?histories.front():uiApp/L"Users";openFolder(path);return 0;}
        case 106:{
            saveUi();if(clientRunning(uiApp)){notice(L"Для согласованной копии сначала выйдите из QIP через меню «Выход».\nОкно настроек можно оставить открытым.");return 0;}
            ProfileBackupStore().create(uiApp,true,true,now(),externalHistory());refreshBackups();log("profile_backup_created");notice(L"Резервная копия профиля и истории создана.");return 0;
        }
        case 107:{auto path=ProfileBackupStore().root();noLinks(path);fs::create_directories(path);openFolder(path);return 0;}
        case 108:{
            auto selected=SendMessageW(backupCombo,CB_GETCURSEL,0,0);require(selected!=CB_ERR && (size_t)selected<uiBackups.size(),"profile_backup_selection");
            if(clientRunning(uiApp)){notice(L"Перед восстановлением выйдите из QIP через меню «Выход».");return 0;}
            if(MessageBoxW(window,L"Заменить настройки и историю файлами из выбранной копии?\n\nПеред восстановлением будет создана свежая копия текущих данных.\nДругие файлы и старые копии не удаляются.",L"Восстановление QIP",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)return 0;
            saveUi();ProfileBackupStore().restore(uiApp,uiBackups.at((size_t)selected),externalHistory());refreshBackups();log("profile_backup_restored");notice(L"Профиль и история восстановлены. Запускайте QIP обычным ярлыком.");return 0;
        }
    }}catch(const std::exception& error){notice(L"Не удалось выполнить действие. Подробности в журнале.\nПри прерванном восстановлении используйте обычный ярлык QIP: он проверит резервную копию до запуска клиента.");log("settings_failed");logFailure(error);}}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,w,l);
}
static void settingsWindow(const fs::path& app,HINSTANCE instance){
    fs::create_directories(dataRoot());uiApp=app;WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=instance;wc.lpszClassName=L"QIPUpdateSettings";wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);RegisterClassW(&wc);
    auto window=CreateWindowW(wc.lpszClassName,L"QIP 2005 — обновления и резервные копии",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,720,570,nullptr,nullptr,instance,nullptr);require(window,"settings_window");
    auto add=[&](const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id){auto h=CreateWindowW(type,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,(HMENU)(INT_PTR)id,instance,nullptr);SendMessageW(h,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);return h;};
    auto current=L"Установленная версия: "+wide(installedVersion(app));add(L"STATIC",current.c_str(),0,20,20,570,24,0);
    autoCheckBox=add(L"BUTTON",L"Проверять и скачивать обновления автоматически",BS_AUTOCHECKBOX|WS_TABSTOP,20,60,570,28,0);
    autoInstallBox=add(L"BUTTON",L"Устанавливать автоматически после закрытия QIP",BS_AUTOCHECKBOX|WS_TABSTOP,20,95,570,28,0);
    backupBox=add(L"BUTTON",L"Создавать копию профиля и истории после выхода (раз в сутки)",BS_AUTOCHECKBOX|WS_TABSTOP,20,130,660,28,0);
    add(L"STATIC",L"Дополнительная папка истории, если она хранится отдельно",0,20,170,660,24,0);
    historyEdit=add(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,20,200,570,26,0);
    add(L"BUTTON",L"Сохранить",BS_PUSHBUTTON|WS_TABSTOP,20,245,130,32,101);
    add(L"BUTTON",L"Проверить сейчас",BS_PUSHBUTTON|WS_TABSTOP,165,245,160,32,102);
    add(L"BUTTON",L"Журнал и копии",BS_PUSHBUTTON|WS_TABSTOP,340,245,150,32,103);
    add(L"BUTTON",L"Закрыть",BS_PUSHBUTTON|WS_TABSTOP,500,245,90,32,104);
    add(L"STATIC",L"История и резервные копии",0,20,300,660,24,0);
    add(L"BUTTON",L"Открыть историю",BS_PUSHBUTTON|WS_TABSTOP,20,330,210,32,105);
    add(L"BUTTON",L"Сделать резервную копию",BS_PUSHBUTTON|WS_TABSTOP,245,330,210,32,106);
    add(L"BUTTON",L"Папка копий",BS_PUSHBUTTON|WS_TABSTOP,470,330,210,32,107);
    backupCombo=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,20,390,450,170,0);
    add(L"BUTTON",L"Восстановить выбранную",BS_PUSHBUTTON|WS_TABSTOP,485,388,195,32,108);
    add(L"STATIC",L"Копирование и восстановление выполняются при закрытом QIP.\nКопии хранятся на этом компьютере и автоматически не удаляются.",0,20,440,660,48,0);
    auto s=settings();SendMessageW(autoCheckBox,BM_SETCHECK,s.value("check_automatically",true)?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(autoInstallBox,BM_SETCHECK,s.value("install_after_exit",false)?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(backupBox,BM_SETCHECK,s.value("backup_after_exit",true)?BST_CHECKED:BST_UNCHECKED,0);SetWindowTextW(historyEdit,wide(s.value("external_history_directory",std::string())).c_str());refreshBackups();
    ShowWindow(window,SW_SHOW);MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    bool quiet=false;
    try{
        SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        int count=0;auto raw=CommandLineToArgvW(GetCommandLineW(),&count);require(raw,"arguments");std::vector<std::wstring> args(raw,raw+count);LocalFree(raw);
        auto mode=count>1?args[1]:(modulePath().filename()==L"QipLauncher.exe"?L"--launch":L"--settings");
        quiet=mode==L"--configure" || mode==L"--backup-for-install" || mode==L"--prepare-profile";
        if(mode==L"--worker"){require(count==3,"arguments");worker(utf8(args[2]));return 0;}
        if(mode==L"--backup-for-install" || mode==L"--restore-backup"){
            require(count==4,"arguments");auto app=validatedApp(args[2]);std::string id=utf8(args[3]);require(std::regex_match(id,std::regex(R"(manual-[0-9A-Za-z_-]{1,80})")),"backup_id");
            require(!appBusy(app,GetCurrentProcessId()),"application_busy");auto dir=dataRoot()/L"backups"/wide(id);noLinks(dir);WindowsIntegration integration(app);
            if(mode==L"--backup-for-install"){Snapshot(dir).create(app,externalHistory(),integration);fs::copy_file(modulePath(),dir/L"QipRecovery.exe");log("manual_install_backup");}
            else{Snapshot(dir).restore(app,integration,json::array({{{"path","QipUpdateAgent.exe"}},{{"path","QipLauncher.exe"}},{{"path","installed-version.json"}}}));log("manual_backup_restored");}return 0;
        }
        auto app=validatedApp(modulePath().parent_path());
        if(mode==L"--configure"){require((count==4 || count==5) && (args[2]==L"0"||args[2]==L"1") && (args[3]==L"0"||args[3]==L"1"),"arguments");auto s=settings();s["check_automatically"]=args[2]==L"1";s["install_after_exit"]=args[3]==L"1";if(count==5){require(args[4]==L"0"||args[4]==L"1","arguments");s["backup_after_exit"]=args[4]==L"1";}saveSettings(s);return 0;}
        if(mode==L"--prepare-profile"){require(count==2,"arguments");Mutex lock(lockName("Install",app),true);require(lock.acquired(),"application_busy");prepareProfile(app);auto s=settings();if(!s.contains("backup_after_exit")){s["backup_after_exit"]=true;saveSettings(s);}return 0;}
        if(mode==L"--backup-profile"){require(count==2,"arguments");ProfileBackupStore().create(app,true,true,now(),externalHistory());return 0;}
        if(mode==L"--launch")launch(app);
        else if(mode==L"--monitor")monitor(app,false);
        else if(mode==L"--check")monitor(app,true);
        else if(mode==L"--settings")settingsWindow(app,instance);
        else throw std::runtime_error("arguments");return 0;
    }catch(const std::exception& error){log("agent_failed");logFailure(error);if(!quiet)notice(L"Действие обновлятора не выполнено. Проверьте журнал.\nДля восстановления доступны резервные копии.");return 1;}
}
