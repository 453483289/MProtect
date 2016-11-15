
// MProtectDlg.h : ͷ�ļ�
//

#pragma once
#include "afxcmn.h"
#include "dlgProcessProtect.h"
#include "dlgProcessIsolate.h"
#include "DriverHelp.h"

// CMProtectDlg �Ի���
class CMProtectDlg : public CDialogEx
{
// ����
public:
	CMProtectDlg(CWnd* pParent = NULL);	// ��׼���캯��

// �Ի�������
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_MPROTECT_DIALOG };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV ֧��


// ʵ��
protected:
	HICON m_hIcon;

	// ���ɵ���Ϣӳ�亯��
	CString GetAppFolder();
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	DECLARE_MESSAGE_MAP()
public:
	int m_CurSelTab; 
	CString g_VdPath;
	std::string g_IniPathA;
	CString m_DriverPath;
	HANDLE m_hMProtect;
	NOTIFY_HANDLE m_NotifyHandle;
	PVOID m_pOldValue;
	CTabCtrl m_tab;
	CDialog* pDialog[10];
	dlgProcessProtect m_dProcessProtect;
	dlgProcessIsolate m_dProcessIsolate;
	CDriverHelp DriverHelp;
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedCancel();
	afx_msg void OnTcnSelchangeTab(NMHDR *pNMHDR, LRESULT *pResult);
	static UINT ThreadSockDriverFunc(LPVOID pParam);
};
