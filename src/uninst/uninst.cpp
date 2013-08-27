static char *uninst_id = 
	"@(#)Copyright (C) H.Shirouzu 1998-2011   uninst.cpp	Ver3.10";
/* ========================================================================
	Project  Name			: Installer for IPMSG32
	Module Name				: Installer Application Class
	Create					: 1998-06-14(Sun)
	Update					: 2011-04-10(Sun)
	Copyright				: H.Shirouzu
	Reference				: 
	======================================================================== */

#include "../tlib/tlib.h"
#include "resource.h"
#include "uninst.h"
#include "../version.h"

/*
	WinMain
*/
int WINAPI WinMain(HINSTANCE hI, HINSTANCE, LPSTR cmdLine, int nCmdShow)
{
	TUninstApp	app(hI, cmdLine, nCmdShow);
	return	app.Run();
}

/*
	�C���X�g�[���A�v���P�[�V�����N���X
*/
TUninstApp::TUninstApp(HINSTANCE _hI, LPSTR _cmdLine, int _nCmdShow) : TApp(_hI, _cmdLine, _nCmdShow)
{
}

TUninstApp::~TUninstApp()
{
}

void TUninstApp::InitWindow(void)
{
	InitCommonControls();
	TDlg *maindlg = new TUninstDlg(cmdLine);
	mainWnd = maindlg;
	maindlg->Create();
}


/*
	���C���_�C�A���O�N���X
*/
TUninstDlg::TUninstDlg(char *cmdLine) : TDlg(UNINSTALL_DIALOG)
{
	runasWnd = NULL;

	char	*p = strstr(cmdLine, "runas=");
	if (p) {
		runasWnd = (HWND)strtoul(p+6, 0, 16);
		if (!runasWnd) PostQuitMessage(0);
	}
}

TUninstDlg::~TUninstDlg()
{
}

/*
	���C���_�C�A���O�p WM_INITDIALOG �������[�`��
*/
BOOL TUninstDlg::EvCreate(LPARAM lParam)
{
	char	title[256], title2[256];
	GetWindowText(title, sizeof(title));
	::wsprintf(title2, "%s ver%s", title, GetVersionStr());
	SetWindowText(title2);

	GetWindowRect(&rect);
	int		cx = ::GetSystemMetrics(SM_CXFULLSCREEN), cy = ::GetSystemMetrics(SM_CYFULLSCREEN);
	int		xsize = rect.right - rect.left, ysize = rect.bottom - rect.top;

	::SetClassLong(hWnd, GCL_HICON, (LONG)::LoadIcon(TApp::GetInstance(), (LPCSTR)SETUP_ICON));
	MoveWindow((cx - xsize)/2, (cy - ysize)/2, xsize, ysize, TRUE);
	Show();

// ���݃f�B���N�g���ݒ�
	char	resetupDir[MAX_PATH_U8];
	GetModuleFileNameU8(NULL, resetupDir, sizeof(resetupDir));
	GetParentDirU8(resetupDir, resetupDir);
	SetDlgItemTextU8(RESETUP_EDIT, resetupDir);

	if (runasWnd) {
		::SendMessage(runasWnd, IPMSG_QUIT_MESSAGE, 0, 0);
		CheckDlgButton(DELPUBKEY_CHECK, 1);
		PostMessage(WM_COMMAND, IDOK, 0);
	}

	return	TRUE;
}

/*
	���C���_�C�A���O�p WM_COMMAND �������[�`��
*/
BOOL TUninstDlg::EvCommand(WORD wNotifyCode, WORD wID, LPARAM hwndCtl)
{
	switch (wID)
	{
	case IDOK:
		UnInstall();
		return	TRUE;

	case IDCANCEL:
		::PostQuitMessage(0);
		return	TRUE;
	}
	return	FALSE;
}

BOOL TUninstDlg::EventApp(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == IPMSG_QUIT_MESSAGE) {
		PostQuitMessage(0);
		return	TRUE;
	}
	return	FALSE;
}

BOOL DeleteKeySet(const char *csp_name, const char *cont_name, DWORD flag)
{
// ���J���̍폜
#ifndef MS_DEF_PROV
typedef unsigned long HCRYPTPROV;
#define MS_DEF_PROV				"Microsoft Base Cryptographic Provider v1.0"
#define MS_ENHANCED_PROV		"Microsoft Enhanced Cryptographic Provider v1.0"
#define CRYPT_DELETEKEYSET      0x00000010
#define CRYPT_MACHINE_KEYSET    0x00000020
#define PROV_RSA_FULL			1
#endif
	static HINSTANCE	advdll;
	static BOOL (WINAPI *pCryptAcquireContext)(HCRYPTPROV *, LPCSTR, LPCSTR, DWORD, DWORD);
	static BOOL (WINAPI *pCryptReleaseContext)(HCRYPTPROV, DWORD);

	if (!advdll) {
		advdll = ::LoadLibrary("advapi32.dll");
		pCryptAcquireContext = (BOOL (WINAPI *)(HCRYPTPROV *, LPCSTR, LPCSTR, DWORD, DWORD))
					 			::GetProcAddress(advdll, "CryptAcquireContextA");
		pCryptReleaseContext = (BOOL (WINAPI *)(HCRYPTPROV, DWORD))
					 			::GetProcAddress(advdll, "CryptReleaseContext");
	}
	if (!pCryptAcquireContext || !pCryptReleaseContext) return TRUE;

	HCRYPTPROV	hCsp = NULL;

	if (!pCryptAcquireContext(&hCsp, cont_name, csp_name, PROV_RSA_FULL, flag|CRYPT_DELETEKEYSET)) {
		if (pCryptAcquireContext(&hCsp, cont_name, csp_name, PROV_RSA_FULL, flag)) {
			pCryptReleaseContext(hCsp, 0);
			return	FALSE;
		}
	}
	return	TRUE;
}

BOOL RunAsAdmin(HWND hWnd)
{
	char	path[MAX_PATH], buf[MAX_BUF];
	::GetModuleFileName(::GetModuleHandle(NULL), path, sizeof(path));
	sprintf(buf, "/runas=%x\n", hWnd);
	ShellExecute(hWnd, "runas", path, buf, NULL, SW_SHOW);
	return TRUE;
}

BOOL TUninstDlg::UnInstall(void)
{
// ���݁A�N������ ipmsg ���I��
	int		st = TerminateIPMsg();

	if (st == 1) return FALSE;
	if (st == 2) {
		if (!IsWinVista() || TIsUserAnAdmin() || !TIsEnableUAC()) {
			MessageBox(GetLoadStr(IDS_CANTTERMINATE), UNINSTALL_STR);
			return FALSE;
		}
		if (MessageBox(GetLoadStr(IDS_REQUIREADMIN_TERM), "",
						MB_OKCANCEL|MB_ICONINFORMATION) != IDOK) return FALSE;
		return	RunAsAdmin(hWnd);
	}

	if (!runasWnd && MessageBox(GetLoadStr(IDS_START), UNINSTALL_STR,
						MB_OKCANCEL|MB_ICONINFORMATION) != IDOK) return	FALSE;

// ���J���폜
	if (IsDlgButtonChecked(DELPUBKEY_CHECK)) {
		BOOL	need_admin = FALSE;
		char	contName[MAX_PATH_U8], userName[MAX_PATH_U8];
		DWORD	size = sizeof(userName);
		::GetUserName(userName, &size);

		::wsprintf(contName, "ipmsg.rsa2048.%s", userName);
		if (!DeleteKeySet(MS_ENHANCED_PROV, contName, CRYPT_MACHINE_KEYSET) ||
			!DeleteKeySet(MS_ENHANCED_PROV, contName, 0)) need_admin = TRUE;

		::wsprintf(contName, "ipmsg.rsa1024.%s", userName);
		if (!DeleteKeySet(MS_ENHANCED_PROV, contName, CRYPT_MACHINE_KEYSET) ||
			!DeleteKeySet(MS_ENHANCED_PROV, contName, 0)) need_admin = TRUE;

		::wsprintf(contName, "ipmsg.rsa512.%s", userName);
		if (!DeleteKeySet(MS_DEF_PROV, contName, CRYPT_MACHINE_KEYSET) ||
			!DeleteKeySet(MS_DEF_PROV, contName, 0)) need_admin = TRUE;

		if (need_admin) {
			if (IsWinVista() && !TIsUserAnAdmin() && TIsEnableUAC()) {
				if (MessageBox(GetLoadStr(IDS_REQUIREADMIN_PUBKEY), "",
					MB_OKCANCEL|MB_ICONINFORMATION) != IDOK) return FALSE;
				return	RunAsAdmin(hWnd);
			}
		}
	}

// �X�^�[�g�A�b�v���f�X�N�g�b�v����폜
	TRegistry	reg(HKEY_CURRENT_USER);
	if (reg.OpenKey(REGSTR_SHELLFOLDERS)) {
		char	buf[MAX_PATH_U8];
		char	*regStr[]	= { REGSTR_STARTUP, REGSTR_PROGRAMS, REGSTR_DESKTOP, NULL };

		for (int i=0; regStr[i]; i++) {
			if (reg.GetStr(regStr[i], buf, sizeof(buf))) {
				if (i == 0) RemoveSameLink(buf);
				::wsprintf(buf + strlen(buf), "\\%s", IPMSG_SHORTCUT_NAME);
				DeleteLink(buf);
			}
		}
		reg.CloseKey();
	}

// ���W�X�g�����烆�[�U�[�ݒ�����폜
	if (reg.ChangeApp(HSTOOLS_STR))
		reg.DeleteChildTree(GetLoadStr(IDS_REGIPMSG));

// ���W�X�g������A�v���P�[�V���������폜
	char	setupDir[MAX_PATH_U8];		// �Z�b�g�A�b�v�f�B���N�g������ۑ�
	GetDlgItemTextU8(RESETUP_EDIT, setupDir, sizeof(setupDir));

	reg.ChangeTopKey(HKEY_LOCAL_MACHINE);
	if (reg.OpenKey(REGSTR_PATH_APPPATHS)) {
		if (reg.OpenKey(IPMSG_EXENAME)) {
			reg.GetStr(REGSTR_PATH, setupDir, sizeof(setupDir));
			reg.CloseKey();
		}
		reg.DeleteKey(IPMSG_EXENAME);
		reg.CloseKey();
	}

// ���W�X�g������A���C���X�g�[�������폜
	if (reg.OpenKey(REGSTR_PATH_UNINSTALL)) {
		reg.DeleteKey(IPMSG_NAME);
		reg.CloseKey();
	}

// �I�����b�Z�[�W
	MessageBox(GetLoadStr(IDS_UNINSTCOMPLETE));

// �C���X�g�[���f�B���N�g�����J��
	if (GetDriveTypeEx(setupDir) != DRIVE_REMOTE)
		ShellExecuteU8(NULL, NULL, setupDir, 0, 0, SW_SHOW);

	::PostQuitMessage(0);
	return	TRUE;
}

/*
	�������e�����V���[�g�J�b�g���폜�i�X�^�[�g�A�b�v�ւ̏d���o�^�悯�j
*/
BOOL RemoveSameLink(const char *dir, char *remove_path)
{
	char				path[MAX_PATH_U8], dest[MAX_PATH_U8], arg[MAX_PATH_U8];
	HANDLE				fh;
	WIN32_FIND_DATA_U8	data;
	BOOL				ret = FALSE;

	::wsprintf(path, "%s\\*.*", dir);
	if ((fh = FindFirstFileU8(path, &data)) == INVALID_HANDLE_VALUE)
		return	FALSE;

	do {
		::wsprintf(path, "%s\\%s", dir, data.cFileName);
		if (ReadLinkU8(path, dest, arg) && *arg == 0) {
			int		dest_len = (int)strlen(dest);
			int		ipmsg_len = (int)strlen(IPMSG_EXENAME);
			if (dest_len > ipmsg_len && strncmpi(dest + dest_len - ipmsg_len, IPMSG_EXENAME, ipmsg_len) == 0) {
				ret = DeleteFileU8(path);
				if (remove_path)
					strcpy(remove_path, path);
			}
		}

	} while (FindNextFileU8(fh, &data));

	::FindClose(fh);
	return	ret;
}


/*
	�����オ���Ă��� IPMSG ���I��
*/
int TUninstDlg::TerminateIPMsg()
{
	BOOL	existFlg = FALSE;

	::EnumWindows(TerminateIPMsgProc, (LPARAM)&existFlg);
	if (existFlg) {
		if (MessageBox(GetLoadStr(IDS_TERMINATE), "", MB_OKCANCEL) == IDCANCEL)
			return	1;
		::EnumWindows(TerminateIPMsgProc, NULL);
	}
	existFlg = FALSE;
	::EnumWindows(TerminateIPMsgProc, (LPARAM)&existFlg);

	return	!existFlg ? 0 : 2;
}

/*
	lParam == NULL ...	�S IPMSG ���I��
	lParam != NULL ...	lParam �� BOOL * �Ƃ݂Ȃ��AIPMSG proccess �����݂���
						�ꍇ�́A������TRUE ��������B
*/
BOOL CALLBACK TerminateIPMsgProc(HWND hWnd, LPARAM lParam)
{
	char	buf[MAX_BUF];

	if (::GetClassName(hWnd, buf, sizeof(buf)) != 0) {
		if (strncmpi(IPMSG_CLASS, buf, strlen(IPMSG_CLASS)) == 0) {
			if (lParam)
				*(BOOL *)lParam = TRUE;		// existFlg;
			else {
				::SendMessage(hWnd, WM_CLOSE, 0, 0);
				for (int i=0; i < 10; i++) {
					Sleep(300);
					if (!IsWindow(hWnd)) break;
				}
			}
		}
	}
	return	TRUE;
}

/*
	�e�f�B���N�g���擾�i�K���t���p�X�ł��邱�ƁBUNC�Ή��j
*/
BOOL GetParentDirU8(const char *srcfile, char *dir)
{
	char	path[MAX_BUF], *fname=NULL;

	if (GetFullPathNameU8(srcfile, sizeof(path), path, &fname) == 0 || fname == NULL)
		return	strcpy(dir, srcfile), FALSE;

	if (fname - path > 3 || path[1] != ':')
		*(fname - 1) = 0;
	else
		*fname = 0;		// C:\ �̏ꍇ

	strcpy(dir, path);
	return	TRUE;
}


/*
	�t�@�C���̕ۑ�����Ă���h���C�u����
*/
UINT GetDriveTypeEx(const char *file)
{
	if (file == NULL)
		return	GetDriveType(NULL);

	if (IsUncFile(file))
		return	DRIVE_REMOTE;

	char	buf[MAX_PATH_U8];
	int		len = (int)strlen(file), len2;

	strcpy(buf, file);
	do {
		len2 = len;
		GetParentDirU8(buf, buf);
		len = (int)strlen(buf);
	} while (len != len2);

	return	GetDriveTypeU8(buf);
}

/*
	�����N�t�@�C���폜
*/
BOOL DeleteLink(LPCSTR path)
{
	char	dir[MAX_PATH_U8];

	if (!DeleteFileU8(path))
		return	FALSE;

	GetParentDirU8(path, dir);
	::SHChangeNotify(SHCNE_UPDATEDIR, SHCNF_PATH|SHCNF_FLUSH, U8toA(dir), NULL);

	return	TRUE;
}

