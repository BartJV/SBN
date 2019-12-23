#include "sbn_udp_if_struct.h"
#include "sbn_udp_if.h"
#include "sbn_udp_events.h"
#include "cfe.h"
#include <network_includes.h>
#include <string.h>
#include <errno.h>

/* at some point this will be replaced by the OSAL network interface */
#ifdef _VXWORKS_OS_
#include "selectLib.h"
#else
#include <sys/select.h>
#endif

int SBN_UDP_Init(int Major, int Minor, int Revision)
{
    if(Major != SBN_UDP_MAJOR || Minor != SBN_UDP_MINOR
        || Revision != SBN_UDP_REVISION)
    {
        CFE_EVS_SendEvent(SBN_UDP_CONFIG_EID, CFE_EVS_ERROR,
                "mismatching version %d.%d.%d (SBN app reports %d.%d.%d)",
                SBN_UDP_MAJOR, SBN_UDP_MINOR, SBN_UDP_REVISION,
                Major, Minor, Revision);
        return SBN_ERROR;
    }/* end if */

    return SBN_SUCCESS;
}/* end SBN_UDP_Init() */

static int ConfAddr(OS_SockAddr_t *Addr, const char *Address)
{
    int32 Status = OS_SUCCESS;

    char AddrHost[OS_MAX_API_NAME];

    char *Colon = strchr(Address, ':');
    int ColonLen = 0;

    if(!Colon || (ColonLen = Colon - Address) >= OS_MAX_API_NAME)
    {
        CFE_EVS_SendEvent(SBN_UDP_CONFIG_EID, CFE_EVS_ERROR,
                "invalid address (Address=%s)", Address);
        return SBN_ERROR;
    }/* end if */

    strncpy(AddrHost, Address, ColonLen);

    char *ValidatePtr = NULL;
    long Port = strtol(Colon + 1, &ValidatePtr, 0);
    if(!ValidatePtr || ValidatePtr == Colon + 1)
    {
        CFE_EVS_SendEvent(SBN_UDP_CONFIG_EID, CFE_EVS_ERROR,
                "invalid port (Address=%s)", Address);
    }/* end if */

    if((Status = OS_SocketAddrInit(Addr, OS_SocketDomain_INET)) != OS_SUCCESS)
    {
        CFE_EVS_SendEvent(SBN_UDP_SOCK_EID, CFE_EVS_ERROR,
            "addr init failed (Status=%d)", Status);
        return SBN_ERROR;
    }/* end if */

    if((Status = OS_SocketAddrFromString(Addr, AddrHost)) != OS_SUCCESS)
    {
        CFE_EVS_SendEvent(SBN_UDP_SOCK_EID, CFE_EVS_ERROR,
            "addr host set failed (AddrHost=%s, Status=%d)", AddrHost, Status);
        return SBN_ERROR;
    }/* end if */

    if((Status = OS_SocketAddrSetPort(Addr, Port)) != OS_SUCCESS)
    {
        CFE_EVS_SendEvent(SBN_UDP_SOCK_EID, CFE_EVS_ERROR,
            "addr port set failed (Port=%ld, Status=%d)", Port, Status);
        return SBN_ERROR;
    }/* end if */

    return SBN_SUCCESS;
}/* end ConfAddr() */

int SBN_UDP_LoadNet(SBN_NetInterface_t *Net, const char *Address)
{
    SBN_UDP_Net_t *NetData = (SBN_UDP_Net_t *)Net->ModulePvt;

    CFE_EVS_SendEvent(SBN_UDP_CONFIG_EID, CFE_EVS_INFORMATION,
        "configuring net (NetData=0x%lx, Address=%s)", (long unsigned int)NetData, Address);

    int Status = ConfAddr(&NetData->Addr, Address);

    if(Status == SBN_SUCCESS)
    {
        CFE_EVS_SendEvent(SBN_UDP_CONFIG_EID, CFE_EVS_INFORMATION,
            "configured (NetData=0x%lx)", (long unsigned int)NetData);
    }/* end if */

    return Status;
}/* end SBN_UDP_LoadNet */

int SBN_UDP_LoadPeer(SBN_PeerInterface_t *Peer, const char *Address)
{
    SBN_UDP_Peer_t *PeerData = (SBN_UDP_Peer_t *)Peer->ModulePvt;

    CFE_EVS_SendEvent(SBN_UDP_CONFIG_EID, CFE_EVS_INFORMATION,
        "configuring peer (PeerData=0x%lx, Address=%s)", (long unsigned int)PeerData, Address);

    int Status = ConfAddr(&PeerData->Addr, Address);

    if(Status == SBN_SUCCESS)
    {
        CFE_EVS_SendEvent(SBN_UDP_CONFIG_EID, CFE_EVS_INFORMATION,
            "configured (PeerData=0x%lx)", (long unsigned int)PeerData);
    }/* end if */

    return Status;
}/* end SBN_UDP_LoadPeer */

/**
 * Initializes an UDP host.
 *
 * @param  Interface data structure containing the file entry
 * @return SBN_SUCCESS on success, error code otherwise
 */
int SBN_UDP_InitNet(SBN_NetInterface_t *Net)
{
    int32 Status = OS_SUCCESS;
    SBN_UDP_Net_t *NetData = (SBN_UDP_Net_t *)Net->ModulePvt;

    CFE_EVS_SendEvent(SBN_UDP_SOCK_EID, CFE_EVS_DEBUG,
        "creating socket (NetData=0x%lx)", (long unsigned int)NetData);

    if((Status = OS_SocketOpen(&(NetData->Socket), OS_SocketDomain_INET, OS_SocketType_DATAGRAM)) != OS_SUCCESS)
    {
        CFE_EVS_SendEvent(SBN_UDP_SOCK_EID, CFE_EVS_ERROR,
            "socket call failed (status=%d)", Status);
        return SBN_ERROR;
    }/* end if */

    if((Status = OS_SocketBind(NetData->Socket, &NetData->Addr)) != OS_SUCCESS)
    {
        CFE_EVS_SendEvent(SBN_UDP_SOCK_EID, CFE_EVS_ERROR,
            "bind call failed (NetData=0x%lx, Socket=%d, Status=%d)",
            (long unsigned int)NetData, NetData->Socket, Status);
        return SBN_ERROR;
    }/* end if */

    return SBN_SUCCESS;
}/* end SBN_UDP_InitNet */

/**
 * Initializes an UDP host or peer data struct depending on the
 * CPU name.
 *
 * @param  Interface data structure containing the file entry
 * @return SBN_SUCCESS on success, error code otherwise
 */
int SBN_UDP_InitPeer(SBN_PeerInterface_t *Peer)
{
    return SBN_SUCCESS;
}/* end SBN_UDP_InitPeer */

int SBN_UDP_PollPeer(SBN_PeerInterface_t *Peer)
{
    OS_time_t CurrentTime;
    OS_GetLocalTime(&CurrentTime);

    if(Peer->Connected)
    {
        if(CurrentTime.seconds - Peer->LastRecv.seconds
            > SBN_UDP_PEER_TIMEOUT)
        {
            CFE_EVS_SendEvent(SBN_UDP_DEBUG_EID, CFE_EVS_INFORMATION,
                "CPU %d disconnected", Peer->ProcessorID);

            SBN_Disconnected(Peer);
            return 0;
        }/* end if */

        if(CurrentTime.seconds - Peer->LastSend.seconds
            > SBN_UDP_PEER_HEARTBEAT)
        {
            CFE_EVS_SendEvent(SBN_UDP_DEBUG_EID, CFE_EVS_INFORMATION,
                "CPU %d - heartbeat", Peer->ProcessorID);
            return SBN_SendNetMsg(SBN_UDP_HEARTBEAT_MSG, 0, NULL, Peer);
        }/* end if */
    }
    else
    {
        if(Peer->ProcessorID < CFE_PSP_GetProcessorId() &&
            CurrentTime.seconds - Peer->LastSend.seconds
                > SBN_UDP_ANNOUNCE_TIMEOUT)
        {
            return SBN_SendNetMsg(SBN_UDP_ANNOUNCE_MSG, 0, NULL, Peer);
        }/* end if */
    }/* end if */
    return SBN_SUCCESS;
}/* end SBN_UDP_PollPeer */

int SBN_UDP_Send(SBN_PeerInterface_t *Peer, SBN_MsgType_t MsgType,
    SBN_MsgSz_t MsgSz, void *Payload)
{
    int32 Status = OS_SUCCESS;
    size_t BufSz = MsgSz + SBN_PACKED_HDR_SZ;
    uint8 Buf[BufSz];

    SBN_UDP_Peer_t *PeerData = (SBN_UDP_Peer_t *)Peer->ModulePvt;
    SBN_NetInterface_t *Net = Peer->Net;
    SBN_UDP_Net_t *NetData = (SBN_UDP_Net_t *)Net->ModulePvt;

    SBN_PackMsg(&Buf, MsgSz, MsgType, CFE_PSP_GetProcessorId(), Payload);

    OS_SockAddr_t Addr;
    if((Status = OS_SocketAddrInit(&Addr, OS_SocketDomain_INET)) != OS_SUCCESS)
    {
        CFE_EVS_SendEvent(SBN_UDP_SOCK_EID, CFE_EVS_ERROR,
            "socket addr init failed (status=%d)", Status);
        return SBN_ERROR;
    }/* end if */

    if((Status = OS_SocketSendTo(NetData->Socket, Buf, BufSz, &NetData->Addr)) != OS_SUCCESS)
    {
        CFE_EVS_SendEvent(SBN_UDP_SOCK_EID, CFE_EVS_ERROR,
            "socket sendto failed (PeerData=0x%lx, Status=%d)", (long unsigned int)PeerData, Status);
        return SBN_ERROR;
    }/* end if */

    return SBN_SUCCESS;
}/* end SBN_UDP_Send */

/* Note that this Recv function is indescriminate, packets will be received
 * from all peers but that's ok, I just inject them into the SB and all is
 * good!
 */
int SBN_UDP_Recv(SBN_NetInterface_t *Net, SBN_MsgType_t *MsgTypePtr,
        SBN_MsgSz_t *MsgSzPtr, SBN_CpuID_t *CpuIDPtr,
        void *Payload)
{
    uint8 RecvBuf[SBN_MAX_PACKED_MSG_SZ];

    SBN_UDP_Net_t *NetData = (SBN_UDP_Net_t *)Net->ModulePvt;

#ifndef SBN_RECV_TASK

    /* task-based peer connections block on reads, otherwise use select */
  
    uint32 StateFlags = 0;

    /* timeout returns an OS error */
    if(OS_SelectSingle(NetData->Socket, &StateFlags, 0) != OS_SUCCESS)
    {
        /* nothing to receive */
        return SBN_IF_EMPTY;
    }/* end if */

#endif /* !SBN_RECV_TASK */

    int Received = OS_SocketRecvFrom(NetData->Socket, (char *)&RecvBuf,
        CFE_SB_MAX_SB_MSG_SIZE, NULL, 0);

    if(Received < 0)
    {
        return SBN_ERROR;
    }/* end if */

    /* each UDP packet is a full SBN message */

    if(SBN_UnpackMsg(&RecvBuf, MsgSzPtr, MsgTypePtr, CpuIDPtr, Payload)
        == FALSE)
    {
        return SBN_ERROR;
    }/* end if */

    SBN_PeerInterface_t *Peer = SBN_GetPeer(Net, *CpuIDPtr);
    if(Peer == NULL)
    {
        return SBN_ERROR;
    }/* end if */

    if(!Peer->Connected)
    {
        SBN_Connected(Peer);
    }/* end if */

    if(*MsgTypePtr == SBN_UDP_DISCONN_MSG)
    {
        SBN_Disconnected(Peer);
    }

    return SBN_SUCCESS;
}/* end SBN_UDP_Recv */

int SBN_UDP_ReportModuleStatus(SBN_ModuleStatusPacket_t *Packet)
{
    return SBN_NOT_IMPLEMENTED;
}/* end SBN_UDP_ReportModuleStatus */

int SBN_UDP_UnloadNet(SBN_NetInterface_t *Net)
{
    SBN_UDP_Net_t *NetData = (SBN_UDP_Net_t *)Net->ModulePvt;

    OS_close(NetData->Socket);

    int PeerIdx = 0;
    for(PeerIdx = 0; PeerIdx < Net->PeerCnt; PeerIdx++)
    {
        SBN_UDP_UnloadPeer(&Net->Peers[PeerIdx]);
    }/* end if */

    return SBN_SUCCESS;
}/* end SBN_UDP_UnloadNet */

int SBN_UDP_UnloadPeer(SBN_PeerInterface_t *Peer)
{
    if(Peer->Connected)
    {
        CFE_EVS_SendEvent(SBN_UDP_DEBUG_EID, CFE_EVS_INFORMATION,
                "peer%d - sending disconnect", Peer->ProcessorID);
            return SBN_SendNetMsg(SBN_UDP_DISCONN_MSG, 0, NULL, Peer);
        SBN_Disconnected(Peer);
    }/* end if */

    return SBN_SUCCESS;
}/* end SBN_UDP_UnloadPeer */
