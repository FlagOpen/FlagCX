#include "adaptor.h"
#include "bootstrap.h"
#include "comm.h"
#include "info.h"
#include "net.h"
#include "proxy.h"
#include "topo.h"
#define ENABLE_TIMER 0
#include "timer.h"

flagcxResult_t flagcxTransportP2pSetup(struct flagcxHeteroComm *comm,
                                       struct flagcxTopoGraph *graph,
                                       int connIndex,
                                       int *highestTransportType /*=NULL*/) {
  flagcxIbHandle *handle = NULL;

  for (int peer = 0; peer < comm->nRanks; peer++) {
    for (int c = 0; c < MAXCHANNELS; c++) {
      if (comm->connectRecv[peer] & (1UL << c)) {
        struct flagcxConnector *conn =
            comm->channels[c].peers[peer]->recv + connIndex;
        FLAGCXCHECK(flagcxCalloc(&conn->proxyConn.connection, 1));
        struct recvNetResources *resources;
        FLAGCXCHECK(flagcxCalloc(&resources, 1));
        FLAGCXCHECK(flagcxCalloc(&handle, 1));
        conn->proxyConn.connection->send = 0;
        conn->proxyConn.connection->transportResources = (void *)resources;
        resources->netDev = comm->netDev;
        flagcxNetIb.listen(resources->netDev, (void *)handle,
                           &resources->netListenComm);
        bootstrapSend(comm->bootstrap, peer, 1001 + c, handle,
                      sizeof(flagcxIbHandle));
        deviceAdaptor->streamCreate(&resources->cpStream);
        resources->buffSizes[0] = REGMRBUFFERSIZE;
        deviceAdaptor->gdrMemAlloc((void **)&resources->buffers[0],
                                   resources->buffSizes[0], NULL);
        FLAGCXCHECK(flagcxProxyCallAsync(comm, &conn->proxyConn,
                                         flagcxProxyMsgConnect, handle,
                                         sizeof(flagcxIbHandle), 0, conn));

        free(handle);
      }

      if (comm->connectSend[peer] & (1UL << c)) {
        struct flagcxConnector *conn =
            comm->channels[c].peers[peer]->send + connIndex;
        FLAGCXCHECK(flagcxCalloc(&conn->proxyConn.connection, 1));
        struct sendNetResources *resources;
        FLAGCXCHECK(flagcxCalloc(&resources, 1));
        FLAGCXCHECK(flagcxCalloc(&handle, 1));
        conn->proxyConn.connection->send = 1;
        conn->proxyConn.connection->transportResources = (void *)resources;
        resources->netDev = comm->netDev;
        bootstrapRecv(comm->bootstrap, peer, 1001 + c, handle,
                      sizeof(flagcxIbHandle));
        handle->stage.comm = comm;
        deviceAdaptor->streamCreate(&resources->cpStream);
        resources->buffSizes[0] = REGMRBUFFERSIZE;
        deviceAdaptor->gdrMemAlloc((void **)&resources->buffers[0],
                                   resources->buffSizes[0], NULL);
        FLAGCXCHECK(flagcxProxyCallAsync(comm, &conn->proxyConn,
                                         flagcxProxyMsgConnect, handle,
                                         sizeof(flagcxIbHandle), 0, conn));

        free(handle);
      }
    }
  }

  for (int peer = 0; peer < comm->nRanks; peer++) {
    for (int c = 0; c < MAXCHANNELS; c++) {
      if (comm->connectRecv[peer] & (1UL << c)) {
        struct flagcxConnector *conn =
            comm->channels[c].peers[peer]->recv + connIndex;
        while (flagcxPollProxyResponse(comm, NULL, NULL, conn) ==
               flagcxInProgress)
          ;
        comm->channels[c].peers[peer]->recv[0].connected = 1;
        comm->connectRecv[peer] ^= (1UL << c);
      }

      if (comm->connectSend[peer] & (1UL << c)) {
        struct flagcxConnector *conn =
            comm->channels[c].peers[peer]->send + connIndex;
        while (flagcxPollProxyResponse(comm, NULL, NULL, conn) ==
               flagcxInProgress)
          ;
        comm->channels[c].peers[peer]->send[0].connected = 1;
        comm->connectSend[peer] ^= (1UL << c);
      }
    }
  }

  return flagcxSuccess;
}
