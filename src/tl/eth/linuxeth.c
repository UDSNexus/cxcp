/*
 * BlueParrot XCP
 *
 * (C) 2007-2021 by Christoph Schueler <github.com/Christoph2,
 *                                      cpu12.gems@googlemail.com>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * s. FLOSS-EXCEPTION.txt
 */

#include "xcp.h"
#include "xcp_hw.h"
#include "xcp_eth.h"


unsigned char XcpTl_RxBuffer[XCP_COMM_BUFLEN];
socklen_t addrSize = sizeof(struct sockaddr_storage);

static XcpTl_ConnectionType XcpTl_Connection;

static bool Xcp_EnableSocketOption(int sock, int option);
static bool Xcp_DisableSocketOption(int sock, int option);
static void * XcpTl_WorkerThread(void * param);

static  bool Xcp_EnableSocketOption(int sock, int option)
{
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
    const char enable = 1;

    if (setsockopt(sock, SOL_SOCKET, option, &enable, sizeof(int)) < 0) {
        return false;
    }
#else
    if (setsockopt(sock, SOL_SOCKET, option, &(const char){1}, sizeof(int)) < 0) {
        return false;
    }
#endif
    return true;
}


static bool Xcp_DisableSocketOption(int sock, int option)
{
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
    const char enable = 0;

    if (setsockopt(sock, SOL_SOCKET, option, &enable, sizeof(int)) < 0) {
        return false;
    }
#else
    if (setsockopt(sock, SOL_SOCKET, option, &(const char){0}, sizeof(int)) < 0) {
        return false;
    }
#endif
    return true;
}


void XcpTl_Init(void)
{
    struct addrinfo hints;
    struct addrinfo * addr_info = NULL;
    char * address = NULL;
    char port[16];
    int sock = 0;
    int ret = 0;

    XcpUtl_ZeroMem(&XcpTl_Connection, sizeof(XcpTl_ConnectionType));
    memset(&hints, 0, sizeof(hints));
    XcpTl_Connection.socketType = Xcp_Options.tcp ? SOCK_STREAM : SOCK_DGRAM;
    sprintf(port, "%d", Xcp_Options.port);
    hints.ai_family = Xcp_Options.ipv6 ? PF_INET6: PF_INET;
    hints.ai_socktype = XcpTl_Connection.socketType;
    hints.ai_flags = AI_NUMERICHOST | AI_PASSIVE;
    ret = getaddrinfo(address, port, &hints, &addr_info);

    if (ret != 0) {
        XcpHw_ErrorMsg("XcpTl_Init::getaddrinfo()", errno);
        return;
    }

    sock = socket(addr_info->ai_family, addr_info->ai_socktype, addr_info->ai_protocol);
    if (sock == -1){
        XcpHw_ErrorMsg("XcpTl_Init::socket()", errno);
        return;
    }
    if (bind(sock, addr_info->ai_addr, addr_info->ai_addrlen) == -1) {
        XcpHw_ErrorMsg("XcpTl_Init::bind()", errno);
        return;
    }
    if (XcpTl_Connection.socketType == SOCK_STREAM) {
        if (listen(sock, 1) == -1) {
            XcpHw_ErrorMsg("XcpTl_Init::listen()", errno);
            return;
        }
    }

    XcpTl_Connection.boundSocket = sock;
    freeaddrinfo(addr_info);
    if (!Xcp_EnableSocketOption(XcpTl_Connection.boundSocket, SO_REUSEADDR)) {
        XcpHw_ErrorMsg("XcpTl_Init:setsockopt(SO_REUSEADDR)", errno);
    }

}

void XcpTl_DeInit(void)
{
    close(XcpTl_Connection.boundSocket);
}

void XcpTl_RxHandler(void)
{
    int recv_len = 0;
    uint16_t dlc = 0;
    socklen_t FromLen = 0;
    struct sockaddr_storage From;
    char hostname[NI_MAXHOST];

    XcpUtl_ZeroMem(XcpTl_RxBuffer, XCP_COMM_BUFLEN);

    if (XcpTl_Connection.socketType == SOCK_STREAM) {
        if (!XcpTl_Connection.connected) {
            FromLen = sizeof(From);
            XcpTl_Connection.connectedSocket = accept(XcpTl_Connection.boundSocket, (struct sockaddr *)&XcpTl_Connection.currentAddress, &FromLen);
            if (XcpTl_Connection.connectedSocket == -1) {
                XcpHw_ErrorMsg("XcpTl_RxHandler::accept()", errno);
                //WSACleanup();
                exit(1);
                return;
            }
            inet_ntop(XcpTl_Connection.currentAddress.ss_family, get_in_addr((struct sockaddr *)&XcpTl_Connection.currentAddress), hostname, sizeof(hostname));
            //printf("server: got connection from %s\n", hostname);
        }
        recv_len = recv(XcpTl_Connection.connectedSocket, (char*)XcpTl_RxBuffer, XCP_COMM_BUFLEN, 0);
        if (recv_len == -1) {
            if (errno != EAGAIN) {
                XcpHw_ErrorMsg("XcpTl_RxHandler::recv()", errno);
                close(XcpTl_Connection.connectedSocket);
                exit(1);
                return;
            }
        }
        if (recv_len == 0) {
            DBG_PRINT1("Client closed connection\n\r");
            close(XcpTl_Connection.connectedSocket);
            Xcp_Disconnect();
            return;
        }
    } else {
        recv_len = recvfrom(XcpTl_Connection.boundSocket, (char*)XcpTl_RxBuffer, XCP_COMM_BUFLEN, 0,
            (struct sockaddr *)&XcpTl_Connection.currentAddress, &addrSize
        );
        if (recv_len == -1)
        {
            XcpHw_ErrorMsg("XcpTl_RxHandler:recvfrom()", errno);
            fflush(stdout);
            exit(1);
        }
        //printf("Received %d bytes from client: ", recv_len);
        //Xcp_Hexdump(XcpTl_RxBuffer, recv_len);
    }
    if (recv_len > 0) {
#if XCP_TRANSPORT_LAYER_LENGTH_SIZE == 1
        dlc = (uint16_t)XcpTl_RxBuffer[0];
#elif XCP_TRANSPORT_LAYER_LENGTH_SIZE == 2
        dlc = XCP_MAKEWORD(XcpTl_RxBuffer[0], XcpTl_RxBuffer[1]);
        //dlc = (uint16_t)*(XcpTl_RxBuffer + 0);
#endif // XCP_TRANSPORT_LAYER_LENGTH_SIZE
        if (!XcpTl_Connection.connected || (XcpTl_VerifyConnection())) {
            Xcp_CtoIn.len = dlc;
            XcpUtl_MemCopy(Xcp_CtoIn.data, XcpTl_RxBuffer + XCP_TRANSPORT_LAYER_BUFFER_OFFSET, recv_len - XCP_TRANSPORT_LAYER_BUFFER_OFFSET);
            Xcp_DispatchCommand(&Xcp_CtoIn);
        }
        if (recv_len < 5) {
            DBG_PRINT2("Error: frame to short: %d\n\r", recv_len);
        } else {

        }
        fflush(stdout);
    }
}

void XcpTl_Send(uint8_t const * buf, uint16_t len)
{

    //XcpUtl_Hexdump(buf,  len);
    XCP_TL_ENTER_CRITICAL();
    if (XcpTl_Connection.socketType == SOCK_DGRAM) {
        if (sendto(XcpTl_Connection.boundSocket, (char const *)buf, len, 0,
            (struct sockaddr const *)&XcpTl_Connection.connectionAddress, addrSize) == -1) {
            XcpHw_ErrorMsg("XcpTl_Send:sendto()", errno);
        }
    } else if (XcpTl_Connection.socketType == SOCK_STREAM) {
        if (send(XcpTl_Connection.connectedSocket, (char const *)buf, len, 0) == -1) {
            XcpHw_ErrorMsg("XcpTl_Send:send()", errno);
            close(XcpTl_Connection.connectedSocket);
        }
    }
    XCP_TL_LEAVE_CRITICAL();
}

