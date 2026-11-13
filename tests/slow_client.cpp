#include <windows.h>
#include <fstream>
static ULONGLONG deadline;
static LRESULT CALLBACK fixtureProc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_TIMER && (GetFileAttributesW(L"stop-fixture")!=INVALID_FILE_ATTRIBUTES || GetTickCount64()>=deadline)){DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    wchar_t cwd[32768];DWORD length=GetCurrentDirectoryW(32768,cwd);
    {std::ofstream file("working-directory.bin",std::ios::binary);file.write((char*)cwd,length*sizeof(wchar_t));}
    {std::ofstream file("starts.txt",std::ios::app);file<<"start\n";}
    bool longRun=GetFileAttributesW(L"keep-running")!=INVALID_FILE_ATTRIBUTES;
    deadline=GetTickCount64()+(longRun?30000:4000);
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=fixtureProc;wc.lpszClassName=L"QipControlledFixture";RegisterClassW(&wc);
    HWND owner=nullptr;
    if(GetFileAttributesW(L"vcl-owner")!=INVALID_FILE_ATTRIBUTES){
        WNDCLASSW application{};application.hInstance=instance;application.lpfnWndProc=DefWindowProcW;application.lpszClassName=L"TApplication";RegisterClassW(&application);
        owner=CreateWindowW(application.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    }
    auto window=CreateWindowW(wc.lpszClassName,L"QIP controlled test fixture",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,200,150,owner,nullptr,instance,nullptr);
    if(!window)return 1;
    if(GetFileAttributesW(L"hidden-fixture")==INVALID_FILE_ATTRIBUTES &&
       (GetFileAttributesW(L"visible-fixture")!=INVALID_FILE_ATTRIBUTES || GetFileAttributesW(L"minimized-fixture")!=INVALID_FILE_ATTRIBUTES))
        ShowWindow(window,GetFileAttributesW(L"minimized-fixture")!=INVALID_FILE_ATTRIBUTES?SW_SHOWMINIMIZED:SW_SHOW);
    SetTimer(window,1,50,nullptr);MSG message;while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return 0;
}
