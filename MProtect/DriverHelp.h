/************************************************************************/
/* Copyright(C)2015,
* All rights reserved
* ��  �ƣ�DriverHelp.h
* ժ  Ҫ������CDriverHelp��
* ��  ����v1.0
* ��  ��: Run
* ����ʱ�䣺2015.03.06
* �޸ļ�¼����
*/
/************************************************************************/


#pragma once


class CDriverHelp
{
public:
	CDriverHelp(void);
	~CDriverHelp(void);


	/**************************************************
	��  ��:LoadDriver@12
	��  ��:����ָ������
	��  ��:	DriverAbsPath - �����������·��
	ServiceName - ������
	DisplayName - ������ʾ��
	����ֵ:�ɹ�:0;ʧ��:����GetLastError������
	˵  ��:	�ⲿ����
	**************************************************/
	DWORD DrvLoadDriver(const TCHAR* v_szDriverAbsPath, 
		              const TCHAR* v_szServiceName, 
		              const TCHAR* v_szDisplayName);

	/**************************************************
	��  ��:UnloadDriver@4
	��  ��:ж��ָ�����Ƶ���������
	��  ��:ServiceName - ���������
	����ֵ:�ɹ�:0;ʧ��:����GetLastError������
	˵  ��:	�ⲿ����
	�Բ����ڵķ��񷵻�-1(�ɹ�)
	**************************************************/
	DWORD DrvUnloadDriver(const TCHAR* v_szServiceName);

	/**************************************************
	��  ��:DrvOpenDriver
	��  ��:������ͨ��
	��  ��:v_szDeviceName -�豸����
	����ֵ:�ɹ�:TRUE; ʧ��:FALSE
	˵  ��:	�ⲿ����
	**************************************************/
	BOOL DrvOpenDriver(const TCHAR* v_szDeviceName);

	/**************************************************
	��  ��:DrvCloseDriver
	��  ��:�ر�����ͨ��
	��  ��:
	����ֵ:�ɹ�:TRUE; ʧ��:FALSE
	˵  ��:	�ⲿ����
	**************************************************/
	BOOL DrvCloseDriver();

	/**************************************************
	��  ��:DrvSendCommand
	��  ��:�������㷢������
	��  ��:v_nCtrlCode  - ������������
	v_szData     - ���͵�����������
	v_iDataLen   - �������ݳ���
	v_szResult   - �������ؽ��
	v_iResultLen - �������ؽ������

	����ֵ:�ɹ�:TRUE; ʧ��:FALSE
	˵  ��:	�ⲿ����
	**************************************************/
	BOOL DrvSendCommand(const DWORD v_nCtrlCode, 
		const char*    v_szData, 
		const DWORD    v_iDataLen, 
		char*          v_szResult, 
		DWORD&         v_iResultLen);


protected:

private:

	SC_HANDLE m_hScManager;  //������ƹ��������
	long      m_nRefcount;
	HANDLE    m_hDevice;     //�豸���

	//�򿪷��������
	DWORD DrvOpenScManager(int open);
	//��������
	DWORD DrvCreateService(const TCHAR* v_szDriverAbsolutePath, 
	const TCHAR* ServiceName,const TCHAR* ServiceDispalyName,SC_HANDLE* phService);
    //��ӷ���
	DWORD DrvAddService(const TCHAR* DriverAbsPath, const TCHAR* ServiceName, 
		const TCHAR* DisplayName);
	//ɾ������
	DWORD DrvDeleteService(const TCHAR* ServiceName);


};
