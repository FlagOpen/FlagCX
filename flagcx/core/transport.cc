#include "adaptor.h"
#include "bootstrap.h"
#include "comm.h"
#include "info.h"
#include "net.h"
#include "p2p.h"
#include "proxy.h"
#include "topo.h"
#include "shmutils.h"
#define ENABLE_TIMER 0
#include "timer.h"

static inline bool isSameNode(struct flagcxHeteroComm *comm, int peer) {
  if (comm->peerInfo == NULL) {
    // peerInfo not initialized - assume different nodes (use network transport)
    return false;
  }
  return comm->peerInfo[peer].hostHash == comm->peerInfo[comm->rank].hostHash;
}

flagcxResult_t flagcxTransportP2pSetup(struct flagcxHeteroComm *comm,
                                       struct flagcxTopoGraph *graph,
                                       int connIndex,
                                       int *highestTransportType /*=NULL*/) {
  flagcxIbHandle *handle = NULL;

  for (int peer = 0; peer < comm->nRanks; peer++) {
    if (peer == comm->rank) continue;
    bool sameNode = isSameNode(comm, peer);
    for (int c = 0; c < MAXCHANNELS; c++) {
      if (comm->connectRecv[peer] & (1UL << c)) {
        struct flagcxConnector *conn =
            comm->channels[c].peers[peer]->recv + connIndex;
        if (sameNode) {
          printf("Code run into sameNode\n");
          FLAGCXCHECK(flagcxCalloc(&conn->proxyConn.connection, 1));
          struct flagcxP2pResources* resources;
          FLAGCXCHECK(flagcxCalloc(&resources, 1));
          conn->proxyConn.connection->transport = TRANSPORT_P2P;
          conn->proxyConn.connection->send = 0;
          conn->proxyConn.connection->transportResources = (void *)resources;
          
          struct flagcxP2pRequest req = {(size_t(REGMRBUFFERSIZE)), 0};
          struct flagcxP2pConnectInfo connectInfo = {0};
          connectInfo.rank = comm->rank;
          connectInfo.read = 0;

          FLAGCXCHECK(flagcxProxyCallBlocking(comm, &conn->proxyConn, flagcxProxyMsgSetup,
                                              &req, sizeof(req), 
                                              &connectInfo.p2pBuff, sizeof(connectInfo.p2pBuff)));
          printf("Receiver Code run after flagcxProxyMsgSetup\n");
          
          // Use the buffer directly without offset
          char* recvBuffer = (char*)connectInfo.p2pBuff.directPtr;
          
          // Set up receiver's buffer
          conn->conn.buffs[FLAGCX_PROTO_SIMPLE] = recvBuffer;
          conn->conn.stepSize = comm->buffSizes[FLAGCX_PROTO_SIMPLE] / MAXSTEPS;
          
          FLAGCXCHECK(bootstrapSend(comm->bootstrap, peer, 2000 + c, 
                                    &connectInfo, sizeof(connectInfo)));
        } else {
          FLAGCXCHECK(flagcxCalloc(&conn->proxyConn.connection, 1));
          struct recvNetResources *resources;
          FLAGCXCHECK(flagcxCalloc(&resources, 1));
          FLAGCXCHECK(flagcxCalloc(&handle, 1));
          conn->proxyConn.connection->transport = TRANSPORT_NET;
          conn->proxyConn.connection->send = 0;
          conn->proxyConn.connection->transportResources = (void *)resources;
          resources->netDev = comm->netDev;
          resources->netAdaptor = comm->netAdaptor;
          comm->netAdaptor->listen(resources->netDev, (void *)handle,
                                   &resources->netListenComm);
          bootstrapSend(comm->bootstrap, peer, 1001 + c, handle,
                        sizeof(flagcxIbHandle));
          deviceAdaptor->streamCreate(&resources->cpStream);
          for (int s = 0; s < MAXSTEPS; s++) {
            deviceAdaptor->eventCreate(&resources->cpEvents[s],
                                       flagcxEventDisableTiming);
          }
          resources->buffSizes[0] = REGMRBUFFERSIZE;
          if (comm->netAdaptor == getUnifiedNetAdaptor(SOCKET)) {
            resources->buffers[0] = (char *)malloc(resources->buffSizes[0]);
            if (!resources->buffers[0]) {
              return flagcxSystemError;
            }
          } else if (comm->netAdaptor == getUnifiedNetAdaptor(IBRC)) {
            deviceAdaptor->gdrMemAlloc((void **)&resources->buffers[0],
                                       resources->buffSizes[0], NULL);
          }
          FLAGCXCHECK(flagcxProxyCallAsync(comm, &conn->proxyConn,
                                           flagcxProxyMsgConnect, handle,
                                           sizeof(flagcxIbHandle), 0, conn));
          free(handle);
        }
      }

      if (comm->connectSend[peer] & (1UL << c)) {
        struct flagcxConnector *conn =
            comm->channels[c].peers[peer]->send + connIndex;
        
        if (sameNode) {
          FLAGCXCHECK(flagcxCalloc(&conn->proxyConn.connection, 1));
          struct flagcxP2pResources *resources;
          FLAGCXCHECK(flagcxCalloc(&resources, 1));
          
          conn->proxyConn.connection->transport = TRANSPORT_P2P;
          conn->proxyConn.connection->send = 1;
          conn->proxyConn.connection->transportResources = (void *)resources;
          
          struct flagcxP2pConnectInfo connectInfo = {0};
          FLAGCXCHECK(flagcxProxyCallBlocking(comm, &conn->proxyConn, flagcxProxyMsgSetup,
                                              NULL, 0, &resources->proxyInfo, sizeof(struct flagcxP2pShmProxyInfo)));
          printf("Sender Code run after flagcxProxyMsgSetup\n");
          memcpy(&connectInfo.desc, &resources->proxyInfo.desc, sizeof(flagcxShmIpcDesc_t));

          INFO(FLAGCX_INIT, "Send: Sending shmDesc to peer %d, shmSuffix=%s shmSize=%zu",
               peer, connectInfo.desc.shmSuffix, connectInfo.desc.shmSize);

          FLAGCXCHECK(bootstrapSend(comm->bootstrap, peer, 3000 + c, 
                                    &connectInfo.desc, sizeof(flagcxShmIpcDesc_t)));
        } else {
          INFO(FLAGCX_INIT, "NET Send setup: rank %d -> peer %d channel %d (different node)", 
               comm->rank, peer, c);
               
          FLAGCXCHECK(flagcxCalloc(&conn->proxyConn.connection, 1));
          struct sendNetResources *resources;
          FLAGCXCHECK(flagcxCalloc(&resources, 1));
          FLAGCXCHECK(flagcxCalloc(&handle, 1));
          conn->proxyConn.connection->send = 1;
          conn->proxyConn.connection->transport = TRANSPORT_NET;
          conn->proxyConn.connection->transportResources = (void *)resources;
          resources->netDev = comm->netDev;
          resources->netAdaptor = comm->netAdaptor;
          bootstrapRecv(comm->bootstrap, peer, 1001 + c, handle,
                        sizeof(flagcxIbHandle));
          handle->stage.comm = comm;
          deviceAdaptor->streamCreate(&resources->cpStream);
          for (int s = 0; s < MAXSTEPS; s++) {
            deviceAdaptor->eventCreate(&resources->cpEvents[s],
                                       flagcxEventDisableTiming);
          }
          resources->buffSizes[0] = REGMRBUFFERSIZE;
          if (comm->netAdaptor == getUnifiedNetAdaptor(SOCKET)) {
            resources->buffers[0] = (char *)malloc(resources->buffSizes[0]);
            if (!resources->buffers[0]) {
              return flagcxSystemError;
            }
          } else if (comm->netAdaptor == getUnifiedNetAdaptor(IBRC)) {
            deviceAdaptor->gdrMemAlloc((void **)&resources->buffers[0],
                                       resources->buffSizes[0], NULL);
          }
          FLAGCXCHECK(flagcxProxyCallAsync(comm, &conn->proxyConn,
                                           flagcxProxyMsgConnect, handle,
                                           sizeof(flagcxIbHandle), 0, conn));
          free(handle);
        }
      }
    }
  }

  for (int peer = 0; peer < comm->nRanks; peer++) {
    if (peer == comm->rank) continue;    
    bool sameNode = isSameNode(comm, peer);
    
    for (int c = 0; c < MAXCHANNELS; c++) {
      if (comm->connectRecv[peer] & (1UL << c)) {
        struct flagcxConnector *conn =
            comm->channels[c].peers[peer]->recv + connIndex;
        
        if (sameNode) {
          struct flagcxP2pResources *resources = 
              (struct flagcxP2pResources*)conn->proxyConn.connection->transportResources;
          
          flagcxShmIpcDesc_t shmDesc = {0};
          FLAGCXCHECK(bootstrapRecv(comm->bootstrap, peer, 3000 + c, 
                                    &shmDesc, sizeof(flagcxShmIpcDesc_t)));
          FLAGCXCHECK(flagcxShmImportShareableBuffer(&shmDesc,
                                                    (void**)&resources->shm,
                                                    NULL,
                                                    &resources->desc));
          resources->proxyInfo.shm = resources->shm;
          memcpy(&resources->proxyInfo.desc, &resources->desc, sizeof(flagcxShmIpcDesc_t));
          
          // Set recvFifo in proxyInfo so proxy can copy data to it
          resources->proxyInfo.recvFifo = conn->conn.buffs[FLAGCX_PROTO_SIMPLE];
          
          FLAGCXCHECK(flagcxProxyCallBlocking(comm, &conn->proxyConn,
                                              flagcxProxyMsgConnect,
                                              NULL, 0, NULL, 0));
          
          comm->channels[c].peers[peer]->recv[0].connected = 1;
        } else {
          while (flagcxPollProxyResponse(comm, NULL, NULL, conn) ==
                 flagcxInProgress)
            ;
          comm->channels[c].peers[peer]->recv[0].connected = 1;
        }
        comm->connectRecv[peer] ^= (1UL << c);
      }

      if (comm->connectSend[peer] & (1UL << c)) {
        struct flagcxConnector *conn =
            comm->channels[c].peers[peer]->send + connIndex;

        if (sameNode) {
          struct flagcxP2pResources *resources = 
              (struct flagcxP2pResources*)conn->proxyConn.connection->transportResources;
          
          struct flagcxP2pConnectInfo connectInfo = {0};
          FLAGCXCHECK(bootstrapRecv(comm->bootstrap, peer, 2000 + c, 
                                    &connectInfo, sizeof(connectInfo)));
          printf("P2P Send: recv connectInfo from peer %d, directPtr=%p, size=%zu", 
               peer, connectInfo.p2pBuff.directPtr, connectInfo.p2pBuff.size);
          
          char* remoteBuffer = NULL;
          if (connectInfo.p2pBuff.directPtr != NULL) {
            // Same process: use direct pointer
            remoteBuffer = (char*)connectInfo.p2pBuff.directPtr;
            printf("connectInfo.p2pBuff.directPtr != NULL\n");
          } else {
            // Different process: import IPC handle
            FLAGCXCHECK(flagcxP2pImportShareableBuffer(
                comm, peer, connectInfo.p2pBuff.size, 
                &connectInfo.p2pBuff.ipcDesc, 
                (void**)&remoteBuffer));
            printf("code run after flagcxP2pImportShareableBuffer\n");
          }
          
          if (remoteBuffer == NULL) {
            WARN("P2P Send: remoteBuffer is NULL after import for peer %d channel %d", peer, c);
            return flagcxInternalError;
          }
          
          // Use the buffer directly without offset
          conn->conn.buffs[FLAGCX_PROTO_SIMPLE] = remoteBuffer;
          conn->conn.stepSize = comm->buffSizes[FLAGCX_PROTO_SIMPLE] / FLAGCX_STEPS;
          
          // Save recvFifo to resources->proxyInfo (for local reference)
          resources->proxyInfo.recvFifo = remoteBuffer;
          
          // Send recvFifo to proxy (proxy will save it in connection->transportResources->recvFifo)
          char* recvFifo = remoteBuffer;
          FLAGCXCHECK(flagcxProxyCallBlocking(comm, &conn->proxyConn,
                                              flagcxProxyMsgConnect,
                                              &recvFifo, sizeof(recvFifo),
                                              NULL, 0));
          
          comm->channels[c].peers[peer]->send[0].connected = 1;
          INFO(FLAGCX_INIT, "P2P Send connected: rank %d -> peer %d ch %d, remoteBuffer %p", 
               comm->rank, peer, c, remoteBuffer);
        } else {
          while (flagcxPollProxyResponse(comm, NULL, NULL, conn) ==
                 flagcxInProgress)
            ;
          comm->channels[c].peers[peer]->send[0].connected = 1;
        }
        comm->connectSend[peer] ^= (1UL << c);
      }
    }
  }
  return flagcxSuccess;
}