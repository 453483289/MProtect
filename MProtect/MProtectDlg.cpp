
// MProtectDlg.cpp : ʵ���ļ�
//

#include "stdafx.h"
#include "MProtect.h"
#include "MProtectDlg.h"
#include "afxdialogex.h"
#include "VAuthLib.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// ����Ӧ�ó��򡰹��ڡ��˵���� CAboutDlg �Ի���

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// �Ի�������
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV ֧��

// ʵ��
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


CString CMProtectDlg::GetAppFolder()
{
	char moduleName[MAX_PATH];
	GetModuleFileNameA(NULL, moduleName, sizeof(moduleName));
	std::string strPath = moduleName;
	int pos = strPath.rfind("\\");
	if (!(pos == std::string::npos || pos == strPath.length() - 1))
		strPath = strPath.substr(0, pos);
	strPath += "\\";
	return (CString)strPath.c_str();
}

std::string GetAppFolder(bool string)
{
	char moduleName[MAX_PATH];
	GetModuleFileNameA(NULL, moduleName, sizeof(moduleName));
	std::string strPath = moduleName;
	int pos = strPath.rfind("\\");
	if (!(pos == std::string::npos || pos == strPath.length() - 1))
		strPath = strPath.substr(0, pos);
	strPath += "\\";
	return strPath;
}

CMProtectDlg::CMProtectDlg(CWnd* pParent /*=NULL*/)
	: CDialogEx(IDD_MPROTECT_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	char key[512] = { 0 };
	char hwid[512] = { 0 };
	/*
	������Ч��
	*/
	g_IniPathA = ::GetAppFolder(true) + "Config.ini";
	g_VdPath = GetAppFolder() + L"VAuth.dll";
	GetPrivateProfileStringA("INI", "KEY", 0, key, 512, g_IniPathA.c_str());
	CopyFile(g_VdPath, L"C:\\Windows\\VAuth.dll", true);
	::Initialize("{06415CFF-552B-4202-9673-85231E98B4E7}");
	int a_reg = ::Auth(key);
	switch (a_reg)
	{
	case 0:
		goto goto_en;
	case -1:
		AfxMessageBox(L"�����ڴ�ע����");
		break;
	case -2:
		AfxMessageBox(L"ע���뱻����");
		break;
	case -3:
		AfxMessageBox(L"�󶨻�������");
		break;
	case -4:
		AfxMessageBox(L"ע����������");
		break;
	case -5:
		AfxMessageBox(L"�ѹ���");
		break;
	default:
		break;
	}
	exit(0);



goto_en:
	DWORD dwRetBytes = 0;
	m_DriverPath = GetAppFolder() + L"DdiMon.sys";
	if (_waccess(m_DriverPath, 0) == -1){
		AfxMessageBox(L"My::ȱ�������ļ�\n");
	}

	DriverHelp.DrvLoadDriver(m_DriverPath, L"MProtect", L"MProtect");
	m_hMProtect = CreateFile(L"\\\\.\\MProtect", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (INVALID_HANDLE_VALUE == m_hMProtect){
		Sleep(200);
		m_hMProtect = CreateFile(L"\\\\.\\MProtect", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ |
			FILE_SHARE_WRITE, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
		if (INVALID_HANDLE_VALUE == m_hMProtect) {
			AfxMessageBox(L"My::��������ʧ��\n");
			return;
		}
	}

	m_NotifyHandle.m_hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_NotifyHandle.m_hNotify = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_NotifyHandle.m_uPass = FALSE;
	//�����¼���r0
	if (0 == DeviceIoControl(m_hMProtect, IOCTL_MPROTECT_EVENT, &m_NotifyHandle, sizeof(NOTIFY_HANDLE), NULL, 0, &dwRetBytes, NULL))
	{
		CloseHandle(m_hMProtect);
		CloseHandle((HANDLE)m_NotifyHandle.m_hEvent);
		CloseHandle((HANDLE)m_NotifyHandle.m_hNotify);
		Wow64RevertWow64FsRedirectionFun Wow64RevertWow64FsRedirection = (Wow64RevertWow64FsRedirectionFun)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "Wow64RevertWow64FsRedirection");
		if (Wow64RevertWow64FsRedirection)
			Wow64RevertWow64FsRedirection(&m_pOldValue);
		AfxMessageBox(L"My::�����¼�ͬ��ʧ��\n");
	}
	/*
	����ͨѶ�߳�
	*/
	AfxBeginThread(ThreadSockDriverFunc, this);
}

void CMProtectDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_TAB, m_tab);
}

BEGIN_MESSAGE_MAP(CMProtectDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDOK, &CMProtectDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDCANCEL, &CMProtectDlg::OnBnClickedCancel)
	ON_NOTIFY(TCN_SELCHANGE, IDC_TAB, &CMProtectDlg::OnTcnSelchangeTab)
END_MESSAGE_MAP()


UINT CMProtectDlg::ThreadSockDriverFunc(LPVOID pParam)
{
	CMProtectDlg *pObj = (CMProtectDlg *)pParam;
	TRY_SOKE soke;
	DWORD dwRetBytes = 0;
	UINT Pass = FALSE;
	int Index = 0, Indexb = 0;
	CString bbV, bbP;
	CString exdude;
	CString indude;
	BOOL bOk = TRUE;
	/*
	ͨѶ���ִ���...
	�ȴ���Ϣ
	*/
	do
	{
		WaitForSingleObject((HANDLE)pObj->m_NotifyHandle.m_hEvent, INFINITE);
		if (0 == DeviceIoControl(pObj->m_hMProtect, IOCTL_MPROTECT_EVENT, NULL, NULL, &soke, sizeof(TRY_SOKE), &dwRetBytes, NULL))
		{
			CloseHandle(pObj->m_hMProtect);
			CloseHandle((HANDLE)pObj->m_NotifyHandle.m_hEvent);
			CloseHandle((HANDLE)pObj->m_NotifyHandle.m_hNotify);

			Wow64RevertWow64FsRedirectionFun Wow64RevertWow64FsRedirection = (Wow64RevertWow64FsRedirectionFun)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "Wow64RevertWow64FsRedirection");
			if (Wow64RevertWow64FsRedirection)
				Wow64RevertWow64FsRedirection(&pObj->m_pOldValue);
			
			AfxMessageBox(L"My::��������ʧ��\n");
			
			goto goto_error;
		}
		


		DeviceIoControl(pObj->m_hMProtect, IOCTL_MPROTECT_USERCHOICE, &Pass, sizeof(UINT), NULL, 0, &dwRetBytes, NULL);
		SetEvent((HANDLE)pObj->m_NotifyHandle.m_hNotify);  //����RING0�������
	} while (true);
	/*
	�ر�����ͨѶ
	*/
	CloseHandle(pObj->m_hMProtect);
	CloseHandle((HANDLE)pObj->m_NotifyHandle.m_hEvent);
	CloseHandle((HANDLE)pObj->m_NotifyHandle.m_hNotify);
	Wow64RevertWow64FsRedirectionFun Wow64RevertWow64FsRedirection = (Wow64RevertWow64FsRedirectionFun)GetProcAddress(GetModuleHandle(L"kernel32.dll"), "Wow64RevertWow64FsRedirection");
	if (Wow64RevertWow64FsRedirection)
		Wow64RevertWow64FsRedirection(&pObj->m_pOldValue);
	return true;
goto_error:
	//AfxMessageBox(L"");
	return false;
}

BOOL CMProtectDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// ��������...���˵�����ӵ�ϵͳ�˵��С�

	// IDM_ABOUTBOX ������ϵͳ���Χ�ڡ�
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// ���ô˶Ի����ͼ�ꡣ  ��Ӧ�ó��������ڲ��ǶԻ���ʱ����ܽ��Զ�
	//  ִ�д˲���
	SetIcon(m_hIcon, TRUE);			// ���ô�ͼ��
	SetIcon(m_hIcon, FALSE);		// ����Сͼ��

	m_tab.InsertItem(0, L"���̱���", 50);
	m_tab.InsertItem(1, L"���̸���", 50);

	m_dProcessProtect.Create(IDD_DIALOG_PROCESS_PROTECT, &m_tab);
	m_dProcessIsolate.Create(IDD_DIALOG_PROCESS_ISOLATE, &m_tab);
	//�趨��Tab����ʾ�ķ�Χ
	CRect rc;
	m_tab.GetClientRect(rc);
	rc.top += 20;
	rc.bottom -= 0;
	rc.left += 0;
	rc.right -= 0;
	m_dProcessProtect.MoveWindow(&rc);
	m_dProcessIsolate.MoveWindow(&rc);
	//�ѶԻ������ָ�뱣������
	pDialog[0] = &m_dProcessProtect;
	pDialog[1] = &m_dProcessIsolate;
	//��ʾ��ʼҳ��
	pDialog[0]->ShowWindow(SW_SHOW);
	pDialog[1]->ShowWindow(SW_HIDE);
	//���浱ǰѡ��
	m_CurSelTab = 0;
	return TRUE;  // ���ǽ��������õ��ؼ������򷵻� TRUE
}

void CMProtectDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// �����Ի��������С����ť������Ҫ����Ĵ���
//  �����Ƹ�ͼ�ꡣ  ����ʹ���ĵ�/��ͼģ�͵� MFC Ӧ�ó���
//  �⽫�ɿ���Զ���ɡ�

void CMProtectDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // ���ڻ��Ƶ��豸������

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// ʹͼ���ڹ����������о���
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// ����ͼ��
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

//���û��϶���С������ʱϵͳ���ô˺���ȡ�ù��
//��ʾ��
HCURSOR CMProtectDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}



void CMProtectDlg::OnBnClickedOk()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	//CDialogEx::OnOK();
}


void CMProtectDlg::OnBnClickedCancel()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	CDialogEx::OnCancel();
}


void CMProtectDlg::OnTcnSelchangeTab(NMHDR *pNMHDR, LRESULT *pResult)
{
	//�ѵ�ǰ��ҳ����������
	pDialog[m_CurSelTab]->ShowWindow(SW_HIDE);
	//�õ��µ�ҳ������
	m_CurSelTab = m_tab.GetCurSel();
	//���µ�ҳ����ʾ����
	pDialog[m_CurSelTab]->ShowWindow(SW_SHOW);
	*pResult = 0;
}
