﻿static char *msgmng_id = 
	"@(#)Copyright (C) H.Shirouzu 1996-2011   msgmng.cpp	Ver3.31";
/* ========================================================================
	Project  Name			: IP Messenger for Win32
	Module Name				: Message Manager
	Create					: 1996-06-01(Sat)
	Update					: 2011-08-21(Sun)
	Copyright				: H.Shirouzu
	Reference				: 
	======================================================================== */

#include <stdio.h>
#include "resource.h"
#include "ipmsg.h"

MsgMng::MsgMng(ULONG nicAddr, int portNo, Cfg *_cfg)
{
	status = FALSE;
	packetNo = (ULONG)Time();
	udp_sd = tcp_sd = INVALID_SOCKET;
	hAsyncWnd = 0;
	local.addr = nicAddr;
	local.portNo = Thtons(portNo);
	cfg = _cfg;

	if (!WSockInit(cfg ? TRUE : FALSE)) return;

	WCHAR	wbuf[128];
	DWORD	size = sizeof(wbuf);
	if (!::GetComputerNameW(wbuf, &size)) {
		GetLastErrorMsg("GetComputerName()");
		return;
	}
	WtoU8(wbuf, local.hostName, sizeof(local.hostName));
	WtoA(wbuf, localA.hostName, sizeof(localA.hostName));

	if (nicAddr == INADDR_ANY)
	{
		char	host[MAX_BUF];
		if (::Tgethostname(host, sizeof(host)) == -1)
			strcpy(host, local.hostName);

		hostent	*ent = ::Tgethostbyname(host);
		if (ent)
			local.addr = *(ULONG *)ent->h_addr_list[0];
	}

	size = sizeof(wbuf);
	if (!::GetUserNameW(wbuf, &size)) {
		GetLastErrorMsg("GetUserName()");
		return;
	}
	WtoU8(wbuf, local.userName, sizeof(local.userName));
	WtoA(wbuf, localA.userName, sizeof(localA.userName));
	orgLocal = local;

	status = TRUE;
}

MsgMng::~MsgMng()
{
	WSockTerm();
}

BOOL MsgMng::WSockInit(BOOL recv_flg)
{
	WSADATA		wsaData;

	if (::TWSAStartup(MAKEWORD(2,2), &wsaData) != 0 &&
		::TWSAStartup(MAKEWORD(1,1), &wsaData) != 0) {
		return	GetSockErrorMsg("WSAStart()"), FALSE;
	}

	if ((udp_sd = ::Tsocket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
		return	GetSockErrorMsg("Please setup TCP/IP(controlpanel->network)\r\n"), FALSE;

	if (!recv_flg)
		return	TRUE;

	if ((tcp_sd = ::Tsocket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
		return	GetSockErrorMsg("Please setup2 TCP/IP(controlpanel->network)\r\n"), FALSE;

	struct sockaddr_in	addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family			= AF_INET;
	addr.sin_addr.s_addr	= local.addr;
	addr.sin_port			= local.portNo;

	if (::Tbind(udp_sd, (LPSOCKADDR)&addr, sizeof(addr)) != 0)
		return	GetSockErrorMsg("bind()"), FALSE;

	if (::Tbind(tcp_sd, (LPSOCKADDR)&addr, sizeof(addr)) != 0)
	{
		::Tclosesocket(tcp_sd);
		tcp_sd = INVALID_SOCKET;
		GetSockErrorMsg("bind(tcp) error. Can't support file attach");
	}

	BOOL	flg = TRUE;	// Non Block
	if (::Tioctlsocket(udp_sd, FIONBIO, (unsigned long *)&flg) != 0)
		return	GetSockErrorMsg("ioctlsocket(nonblock)"), FALSE;

	if (IsAvailableTCP() && ::Tioctlsocket(tcp_sd, FIONBIO, (unsigned long *)&flg) != 0)
		return	GetSockErrorMsg("ioctlsocket tcp(nonblock)"), FALSE;

	flg = TRUE;			// allow broadcast
	if (::Tsetsockopt(udp_sd, SOL_SOCKET, SO_BROADCAST, (char *)&flg, sizeof(flg)) != 0)
		return	GetSockErrorMsg("setsockopt(broadcast)"), FALSE;

	int	buf_size = MAX_SOCKBUF, buf_minsize = MAX_SOCKBUF / 2;		// UDP バッファ設定
	if (::Tsetsockopt(udp_sd, SOL_SOCKET, SO_SNDBUF, (char *)&buf_size, sizeof(int)) != 0
	&&	::Tsetsockopt(udp_sd, SOL_SOCKET, SO_SNDBUF, (char *)&buf_minsize, sizeof(int)) != 0)
		GetSockErrorMsg("setsockopt(sendbuf)");

	buf_size = MAX_SOCKBUF, buf_minsize = MAX_SOCKBUF / 2;
	if (::Tsetsockopt(udp_sd, SOL_SOCKET, SO_RCVBUF, (char *)&buf_size, sizeof(int)) != 0
	&&	::Tsetsockopt(udp_sd, SOL_SOCKET, SO_RCVBUF, (char *)&buf_minsize, sizeof(int)) != 0)
		GetSockErrorMsg("setsockopt(recvbuf)");

	flg = TRUE;	// REUSE ADDR
	if (IsAvailableTCP() && ::Tsetsockopt(tcp_sd, SOL_SOCKET, SO_REUSEADDR, (char *)&flg, sizeof(flg)) != 0)
		GetSockErrorMsg("setsockopt tcp(reuseaddr)");

	if (IsAvailableTCP() && ::Tlisten(tcp_sd, 100) != 0)
		return	FALSE;

	return	TRUE;
}

void MsgMng::WSockTerm(void)
{
	CloseSocket();
	TWSACleanup();
}

void MsgMng::CloseSocket(void)
{
	if (udp_sd != INVALID_SOCKET)
	{
		::Tclosesocket(udp_sd);
		udp_sd = INVALID_SOCKET;
	}
	if (tcp_sd != INVALID_SOCKET)
	{
		::Tclosesocket(tcp_sd);
		tcp_sd = INVALID_SOCKET;
	}
}

BOOL MsgMng::WSockReset(void)
{
	WSockTerm();
	return	WSockInit(TRUE);
}

BOOL MsgMng::Send(HostSub *hostSub, ULONG command, int val)
{
	char	buf[MAX_NAMEBUF];

	wsprintf(buf, "%d", val);
	return	Send(hostSub, command, buf);
}

BOOL MsgMng::Send(HostSub *hostSub, ULONG command, const char *message, const char *exMsg)
{
	return	Send(hostSub->addr, hostSub->portNo, command, message, exMsg);
}

BOOL MsgMng::Send(ULONG host, int port_no, ULONG command, const char *message, const char *exMsg)
{
	char	buf[MAX_UDPBUF];
	int		trans_len;

	MakeMsg(buf, command, message, exMsg, &trans_len);
	return	UdpSend(host, port_no, buf, trans_len);
}

BOOL MsgMng::AsyncSelectRegister(HWND hWnd)
{
	if (hAsyncWnd == 0)
		hAsyncWnd = hWnd;

	if (::TWSAAsyncSelect(udp_sd, hWnd, WM_UDPEVENT, FD_READ) == SOCKET_ERROR)
		return	FALSE;

	if (::TWSAAsyncSelect(tcp_sd, hWnd, WM_TCPEVENT, FD_ACCEPT|FD_CLOSE) == SOCKET_ERROR)
		return	FALSE;

	return	TRUE;
}

BOOL MsgMng::Recv(MsgBuf *msg)
{
	RecvBuf		buf;

	if (!UdpRecv(&buf) || buf.size == 0)
		return	FALSE;

	return	ResolveMsg(&buf, msg);
}

ULONG MsgMng::MakeMsg(char *buf, int _packetNo, ULONG command, const char *msg, const char *exMsg, int *packet_len)
{
	int			cmd = GET_MODE(command);
	BOOL		is_br_cmd =	cmd == IPMSG_BR_ENTRY ||
							cmd == IPMSG_BR_EXIT  ||
							cmd == IPMSG_BR_ABSENCE ||
							cmd == IPMSG_NOOPERATION ? TRUE : FALSE;
	BOOL		is_utf8		= (command & IPMSG_UTF8OPT);
	HostSub		*host		= is_utf8 ? &local : &localA;
	char		*out_buf	= NULL;
	char		*out_exbuf	= NULL;
	const char	*msg_org	= msg;
	const char	*exMsg_org	= exMsg;
	int			len			= 0;
	int			ex_len		= 0;
	int			&pkt_len	= packet_len ? *packet_len : len;
	int			max_len		= MAX_UDPBUF;
	BOOL		is_ascii;

	pkt_len = sprintf(buf, "%d:%u:%s:%s:%u:", IPMSG_VERSION, _packetNo,
							host->userName, host->hostName, command);

	if (msg) {
		if (!is_utf8 && IsUTF8(msg, &is_ascii) && !is_ascii) {
			msg = out_buf = U8toA(msg, TRUE);
		}
	}

	if (exMsg) {
		if (!is_utf8 && IsUTF8(exMsg, &is_ascii) && !is_ascii) {
			exMsg = out_exbuf = U8toA(exMsg, TRUE);
		}
		ex_len = (int)strlen(exMsg);
	}

	if (ex_len + pkt_len + 2 >= MAX_UDPBUF)
		ex_len = 0;
	max_len -= ex_len;

	if (msg) {
		pkt_len += LocalNewLineToUnix(msg, buf + pkt_len, max_len - pkt_len);
	}

	pkt_len++;

	if (ex_len) {
		memcpy(buf + pkt_len, exMsg, ex_len);
		pkt_len += ex_len;
	}

	if (is_br_cmd) {
		buf[pkt_len++] = 0;
		pkt_len += sprintf(buf + pkt_len, "\nUN:%s\nHN:%s\nNN:%s\nGN:%s",
						local.userName, local.hostName, msg_org, exMsg_org);
	}

	delete [] out_exbuf;
	delete [] out_buf;

	return	_packetNo;
}

BOOL MsgMng::ResolveMsg(RecvBuf *buf, MsgBuf *msg)
{
	char	*exStr  = NULL, *tok, *p;
	char	*exStr2 = NULL;
	char	*userName, *hostName;
	int		len, exLen = 0;

	len = (int)strlen(buf->msgBuf); // main message

	if (buf->size > len + 1) { // ex message (group name or attached file)
		exStr = buf->msgBuf + len + 1;
		exLen = (int)strlen(exStr);
		if (buf->size > len + 1 + exLen + 1) { // ex2 message (utf8 entry)
			exStr2 = exStr + exLen + 1;
		}
	}

	msg->hostSub.addr	= buf->addr.sin_addr.s_addr;
	msg->hostSub.portNo	= buf->addr.sin_port;

	if ((tok = separate_token(buf->msgBuf, ':', &p)) == NULL)
		return	FALSE;
	if ((msg->version = atoi(tok)) != IPMSG_VERSION)
		return	FALSE;

	if ((tok = separate_token(NULL, ':', &p)) == NULL)
		return	FALSE;
	msg->packetNo = strtoul(tok, 0, 10);
	strncpyz(msg->packetNoStr, tok, sizeof(msg->packetNoStr)); // for IV

	if ((userName = separate_token(NULL, ':', &p)) == NULL)
		return	FALSE;

	if ((hostName = separate_token(NULL, ':', &p)) == NULL)
		return	FALSE;

	if ((tok = separate_token(NULL, ':', &p)) == NULL)
		return	FALSE;
	msg->command = atol(tok);
	BOOL	is_utf8 = (msg->command & IPMSG_UTF8OPT);

	strncpyz(msg->hostSub.userName, is_utf8 ? userName : AtoU8(userName), sizeof(msg->hostSub.userName));
	strncpyz(msg->hostSub.hostName, is_utf8 ? hostName : AtoU8(hostName), sizeof(msg->hostSub.hostName));

	int		cnt = 0;
	*msg->msgBuf = 0;
	if ((tok = separate_token(NULL, 0, &p))) // 改行をUNIX形式からDOS形式に変換
	{
		if (!is_utf8) {
			tok = AtoU8(tok);
		}
		UnixNewLineToLocal(tok, msg->msgBuf, MAX_UDPBUF);
	}

	if (exStr) {
		if (exStr[0] != '\n') {
			if ((msg->command & IPMSG_UTF8OPT) == 0) {
				exStr = AtoU8(exStr);
			}
			strncpyz(msg->exBuf, exStr, sizeof(msg->exBuf));
		}
		else if (exStr2 == NULL) {
			exStr2 = exStr;
		}

		if (exStr2 && exStr2[0] == '\n' && (msg->command & IPMSG_CAPUTF8OPT)) {
			for (tok=separate_token(exStr2, '\n', &p); tok; tok=separate_token(NULL, '\n', &p)) {
				if      (strncmp(tok, "UN:", 3) == 0) {
					strncpyz(msg->hostSub.userName, tok+3, sizeof(msg->hostSub.userName));
				}
				else if (strncmp(tok, "HN:", 3) == 0) {
					strncpyz(msg->hostSub.hostName, tok+3, sizeof(msg->hostSub.hostName));
				}
				else if (strncmp(tok, "NN:", 3) == 0) {
					switch (GET_MODE(msg->command)) {
					case IPMSG_BR_ENTRY: case IPMSG_BR_ABSENCE:
						strncpyz(msg->msgBuf, tok+3, sizeof(msg->msgBuf));
						break;
					}
				}
				else if (strncmp(tok, "GN:", 3) == 0) {
					switch (GET_MODE(msg->command)) {
					case IPMSG_BR_ENTRY: case IPMSG_BR_ABSENCE:
						strncpyz(msg->exBuf, tok+3, sizeof(msg->exBuf));
						break;
					}
				}
			}
		}
	}

	return	TRUE;
}

ULONG MsgMng::MakePacketNo(void)
{
	ULONG now = (ULONG)Time();

	if (now > packetNo) {
		packetNo = now;
	}

	return packetNo++;
}


BOOL MsgMng::UdpSend(ULONG host_addr, int port_no, const char *buf)
{
	return	UdpSend(host_addr, port_no, buf, (int)strlen(buf) +1);
}

BOOL MsgMng::UdpSend(ULONG host_addr, int port_no, const char *buf, int len)
{
	struct sockaddr_in	addr;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family			= AF_INET;
	addr.sin_port			= port_no;
	addr.sin_addr.s_addr	= host_addr;

	if (::Tsendto(udp_sd, buf, len, 0, (LPSOCKADDR)&addr, sizeof(addr)) == SOCKET_ERROR)
	{
		switch (TWSAGetLastError()) {
		case WSAENETDOWN:
			break;
		case WSAEHOSTUNREACH:
			static	BOOL	done;
			if (!done) {
				done = TRUE;
//				MessageBox(0, GetLoadStr(IDS_HOSTUNREACH), ::Tinet_ntoa(*(LPIN_ADDR)&host_addr), MB_OK);
			}
			return	FALSE;
		default:
			return	FALSE;
		}

		if (!WSockReset())
			return	FALSE;

		if (hAsyncWnd && !AsyncSelectRegister(hAsyncWnd))
			return	FALSE;

		if (::Tsendto(udp_sd, buf, len, 0, (LPSOCKADDR)&addr, sizeof(addr)) == SOCKET_ERROR)
			return	FALSE;
	}

	return	TRUE;
}


BOOL MsgMng::UdpRecv(RecvBuf *buf)
{
	buf->addrSize = sizeof(buf->addr);

	if ((buf->size = ::Trecvfrom(udp_sd, buf->msgBuf, sizeof(buf->msgBuf) -1, 0, (LPSOCKADDR)&buf->addr, &buf->addrSize)) == SOCKET_ERROR)
		return	FALSE;
	buf->msgBuf[buf->size] = 0;

	return	TRUE;
}

BOOL MsgMng::Accept(HWND hWnd, ConnectInfo *info)
{
	struct sockaddr_in	addr;
	int		size = sizeof(addr), flg=TRUE;
	if ((info->sd = ::Taccept(tcp_sd, (LPSOCKADDR)&addr, &size)) == INVALID_SOCKET)
		return	FALSE;
	::Tsetsockopt(info->sd, SOL_SOCKET, TCP_NODELAY, (char *)&flg, sizeof(flg));

	info->addr = addr.sin_addr.s_addr;
	info->port = addr.sin_port;
	info->server = info->complete = TRUE;

	for (int buf_size=cfg->TcpbufMax; buf_size > 0; buf_size /= 2)
		if (::Tsetsockopt(info->sd, SOL_SOCKET, SO_SNDBUF, (char *)&buf_size, sizeof(buf_size)) == 0)
			break;

	if (AsyncSelectConnect(hWnd, info))
	{
		info->startTick = info->lastTick = ::GetTickCount();
		return	TRUE;
	}

	::Tclosesocket(info->sd);
	return	FALSE;
}

BOOL MsgMng::Connect(HWND hWnd, ConnectInfo *info)
{
	info->server = FALSE;
	if ((info->sd = ::Tsocket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
		return	FALSE;

	BOOL	flg = TRUE;	// Non Block
	if (::Tioctlsocket(info->sd, FIONBIO, (unsigned long *)&flg) != 0)
		return	FALSE;
	::Tsetsockopt(info->sd, SOL_SOCKET, TCP_NODELAY, (char *)&flg, sizeof(flg));

	for (int buf_size=cfg->TcpbufMax; buf_size > 0; buf_size /= 2)
		if (::Tsetsockopt(info->sd, SOL_SOCKET, SO_RCVBUF, (char *)&buf_size, sizeof(buf_size)) == 0)
			break;

	if (AsyncSelectConnect(hWnd, info))
	{
		struct sockaddr_in	addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family			= AF_INET;
		addr.sin_port			= info->port;
		addr.sin_addr.s_addr	= info->addr;
		if ((info->complete = (::Tconnect(info->sd, (LPSOCKADDR)&addr, sizeof(addr)) == 0)) || TWSAGetLastError() == WSAEWOULDBLOCK)
		{
			info->startTick = info->lastTick = ::GetTickCount();
			return	TRUE;
		}
	}
	::Tclosesocket(info->sd);
	return	FALSE;
}

BOOL MsgMng::AsyncSelectConnect(HWND hWnd, ConnectInfo *info)
{
	if (::TWSAAsyncSelect(info->sd, hWnd, WM_TCPEVENT, (info->server ? FD_READ : FD_CONNECT)|FD_CLOSE) == SOCKET_ERROR)
		return	FALSE;
	return	TRUE;
}

/*
	非同期系の抑制
*/
BOOL MsgMng::ConnectDone(HWND hWnd, ConnectInfo *info)
{
	::TWSAAsyncSelect(info->sd, hWnd, 0, 0);	// 非同期メッセージの抑制
	BOOL	flg = FALSE;
	::Tioctlsocket(info->sd, FIONBIO, (unsigned long *)&flg);
	return	TRUE;
}

#define ANY_SIZE 1

typedef struct _MIB_IPADDRROW {
  DWORD          dwAddr;
  DWORD          dwIndex;
  DWORD          dwMask;
  DWORD          dwBCastAddr;
  DWORD          dwReasmSize;
  unsigned short unused1;
  unsigned short wType;
} MIB_IPADDRROW, *PMIB_IPADDRROW;

typedef struct _MIB_IPADDRTABLE {
  DWORD         dwNumEntries;
  MIB_IPADDRROW table[ANY_SIZE];
} MIB_IPADDRTABLE, *PMIB_IPADDRTABLE;

AddrInfo *GetIPAddrs(BOOL strict, int *num)
{
	static DWORD (WINAPI *pGetIpAddrTable)(PMIB_IPADDRTABLE, ULONG *, BOOL) = NULL;
	static HMODULE	hMod;

	PMIB_IPADDRTABLE iat = NULL;
	DWORD		dwSize;
	AddrInfo	*ret = NULL;
	int			i, j;
	int			&di = *num;

	di = 0;

	if (!hMod) {
		if (!(hMod = LoadLibrary("iphlpapi.dll"))) return NULL;
	}
	if (!pGetIpAddrTable) {
		pGetIpAddrTable = (DWORD (WINAPI *)(PMIB_IPADDRTABLE, ULONG *, BOOL))GetProcAddress(hMod, "GetIpAddrTable");
		if (!pGetIpAddrTable) return NULL;
	}
	if (pGetIpAddrTable(NULL, &dwSize, 0) != ERROR_INSUFFICIENT_BUFFER) return NULL;

	DynBuf	buf(dwSize);
	iat = (PMIB_IPADDRTABLE)(void *)buf;
	if (pGetIpAddrTable(iat, &dwSize, 0) != NO_ERROR || iat->dwNumEntries == 0) return NULL;

	ret = new AddrInfo[iat->dwNumEntries * sizeof(AddrInfo)];

	for (i=0; i < (int)iat->dwNumEntries; i++) {
		if (iat->table[i].dwAddr == 0x0100007f) continue;
		if (iat->table[i].dwMask == 0xffffffff) continue;
		ret[di].addr    = iat->table[i].dwAddr;
		ret[di].mask    = iat->table[i].dwMask;
		ret[di].br_addr = (iat->table[i].dwAddr & iat->table[i].dwMask) | ~iat->table[i].dwMask;
		if (strict) {
			for (j=0; j < di; j++) {
				if (ret[j].br_addr == ret[di].br_addr) break;
			}
			if (j < di) continue;
		}
		di++;
	}

	return	ret;
}

