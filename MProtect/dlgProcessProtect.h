#pragma once
#include "afxwin.h"
#include "afxcmn.h"


// dlgProcessProtect �Ի���

class dlgProcessProtect : public CDialogEx
{
	DECLARE_DYNAMIC(dlgProcessProtect)

public:
	dlgProcessProtect(CWnd* pParent = NULL);   // ��׼���캯��
	virtual ~dlgProcessProtect();

// �Ի�������
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_DIALOG_PROCESS_PROTECT };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV ֧��

	DECLARE_MESSAGE_MAP()
public:
	afx_msg void OnCbnDropdownComboProtctProcessList();
	CComboBox m_combox_processlist;
	afx_msg void OnCbnSelchangeComboProtctProcessList();
	CButton m_button_add;
	afx_msg void OnBnClickedButtonProcessProtectAdd();
	CListCtrl m_list_process;
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedButtonProcessProtectConfirm();
};
