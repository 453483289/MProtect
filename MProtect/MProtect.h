
// MProtect.h : PROJECT_NAME Ӧ�ó������ͷ�ļ�
//

#pragma once

#ifndef __AFXWIN_H__
	#error "�ڰ������ļ�֮ǰ������stdafx.h�������� PCH �ļ�"
#endif

#include "resource.h"		// ������
#include "MProtectDlg.h"

// CMProtectApp: 
// �йش����ʵ�֣������ MProtect.cpp
//

class CMProtectApp : public CWinApp
{
public:
	CMProtectApp();

// ��д
public:
	virtual BOOL InitInstance();
	CMProtectDlg dlg;
// ʵ��

	DECLARE_MESSAGE_MAP()
};

extern CMProtectApp theApp;