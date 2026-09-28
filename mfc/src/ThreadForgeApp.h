#pragma once
#include <afxwin.h>

class CThreadForgeApp : public CWinApp {
public:
    BOOL InitInstance() override;
};

extern CThreadForgeApp theApp;
