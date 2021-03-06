/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define _DEFAULT_SOURCE
#include "os.h"
#include "taoserror.h"
#include "trpc.h"
#include "tsync.h"
#include "mpeer.h"
#include "mgmtShell.h"
#include "mgmtUser.h"

static int32_t mgmtGetMnodeMeta(STableMetaMsg *pMeta, SShowObj *pShow, void *pConn);
static int32_t mgmtRetrieveMnodes(SShowObj *pShow, char *data, int32_t rows, void *pConn);

#ifndef _MPEER

static SMnodeObj tsMnodeObj = {0};

int32_t mpeerInitMnodes() {
  tsMnodeObj.mnodeId     = 1;
  tsMnodeObj.privateIp   = inet_addr(tsPrivateIp);
  tsMnodeObj.publicIp    = inet_addr(tsPublicIp);
  tsMnodeObj.createdTime = taosGetTimestampMs();
  tsMnodeObj.role        = TAOS_SYNC_ROLE_MASTER;
  tsMnodeObj.port        = tsMnodeDnodePort;
  sprintf(tsMnodeObj.mnodeName, "m%d", tsMnodeObj.mnodeId);
  
  return TSDB_CODE_SUCCESS;
}

void    mpeerCleanupMnodes() {}
int32_t mpeerAddMnode(int32_t dnodeId) { return TSDB_CODE_SUCCESS; }
int32_t mpeerRemoveMnode(int32_t dnodeId) { return TSDB_CODE_SUCCESS; }
void *  mpeerGetMnode(int32_t mnodeId) { return &tsMnodeObj; }
int32_t mpeerGetMnodesNum() { return 1; }
void    mpeerReleaseMnode(struct _mnode_obj *pMnode) {}
bool    mpeerIsMaster() { return tsMnodeObj.role == TAOS_SYNC_ROLE_MASTER; }
void    mpeerUpdateSync() {}

void *mpeerGetNextMnode(void *pNode, SMnodeObj **pMnode) {
  if (*pMnode == NULL) {
    *pMnode = &tsMnodeObj;
  } else {
    *pMnode = NULL;
  }

  return *pMnode;
}

void mpeerGetPrivateIpList(SRpcIpSet *ipSet) {
  ipSet->inUse = 0;
  ipSet->numOfIps = 1;
  ipSet->port = htons(tsMnodeObj.port);
  ipSet->ip[0] = htonl(tsMnodeObj.privateIp);
}

void mpeerGetPublicIpList(SRpcIpSet *ipSet) {
  ipSet->inUse = 0;
  ipSet->numOfIps = 1;
  ipSet->port = htons(tsMnodeObj.port);
  ipSet->ip[0] = htonl(tsMnodeObj.publicIp);
}

void mpeerGetMpeerInfos(void *param) {
  SDMNodeInfos *mpeers = param;
  mpeers->inUse = 0;
  mpeers->nodeNum = 1;
  mpeers->nodeInfos[0].nodeId = htonl(tsMnodeObj.mnodeId);
  mpeers->nodeInfos[0].nodeIp = htonl(tsMnodeObj.privateIp);
  mpeers->nodeInfos[0].nodePort = htons(tsMnodeObj.port);
  strcpy(mpeers->nodeInfos[0].nodeName, tsMnodeObj.mnodeName);
}

int32_t mpeerForwardReqToPeer(void *pHead) {
  return TSDB_CODE_SUCCESS;
}

#endif

int32_t mpeerInit() {
  mgmtAddShellShowMetaHandle(TSDB_MGMT_TABLE_MNODE, mgmtGetMnodeMeta);
  mgmtAddShellShowRetrieveHandle(TSDB_MGMT_TABLE_MNODE, mgmtRetrieveMnodes);
  return mpeerInitMnodes();
}

void mpeerCleanup() {
  mpeerCleanupMnodes();
}

static char *mpeerGetMnodeRoleStr(int32_t role) {
  switch (role) {
    case TAOS_SYNC_ROLE_OFFLINE:
      return "offline";
    case TAOS_SYNC_ROLE_UNSYNCED:
      return "unsynced";
    case TAOS_SYNC_ROLE_SLAVE:
      return "slave";
    case TAOS_SYNC_ROLE_MASTER:
      return "master";
    default:
      return "undefined";
  }
}

static int32_t mgmtGetMnodeMeta(STableMetaMsg *pMeta, SShowObj *pShow, void *pConn) {
  SUserObj *pUser = mgmtGetUserFromConn(pConn, NULL);
  if (pUser == NULL) return 0;

  if (strcmp(pUser->pAcct->user, "root") != 0) return TSDB_CODE_NO_RIGHTS;

  int32_t  cols = 0;
  SSchema *pSchema = pMeta->schema;

  pShow->bytes[cols] = 2;
  pSchema[cols].type = TSDB_DATA_TYPE_SMALLINT;
  strcpy(pSchema[cols].name, "id");
  pSchema[cols].bytes = htons(pShow->bytes[cols]);
  cols++;

  pShow->bytes[cols] = 16;
  pSchema[cols].type = TSDB_DATA_TYPE_BINARY;
  strcpy(pSchema[cols].name, "private ip");
  pSchema[cols].bytes = htons(pShow->bytes[cols]);
  cols++;

  pShow->bytes[cols] = 16;
  pSchema[cols].type = TSDB_DATA_TYPE_BINARY;
  strcpy(pSchema[cols].name, "public ip");
  pSchema[cols].bytes = htons(pShow->bytes[cols]);
  cols++;

  pShow->bytes[cols] = 8;
  pSchema[cols].type = TSDB_DATA_TYPE_TIMESTAMP;
  strcpy(pSchema[cols].name, "create time");
  pSchema[cols].bytes = htons(pShow->bytes[cols]);
  cols++;

  pShow->bytes[cols] = 10;
  pSchema[cols].type = TSDB_DATA_TYPE_BINARY;
  strcpy(pSchema[cols].name, "role");
  pSchema[cols].bytes = htons(pShow->bytes[cols]);
  cols++;

  pMeta->numOfColumns = htons(cols);
  pShow->numOfColumns = cols;

  pShow->offset[0] = 0;
  for (int32_t i = 1; i < cols; ++i) {
    pShow->offset[i] = pShow->offset[i - 1] + pShow->bytes[i - 1];
  }

  pShow->numOfRows = mpeerGetMnodesNum();
  pShow->rowSize = pShow->offset[cols - 1] + pShow->bytes[cols - 1];
  pShow->pNode = NULL;
  mgmtReleaseUser(pUser);

  return 0;
}

static int32_t mgmtRetrieveMnodes(SShowObj *pShow, char *data, int32_t rows, void *pConn) {
  int32_t    numOfRows = 0;
  int32_t    cols      = 0;
  SMnodeObj *pMnode   = NULL;
  char      *pWrite;
  char       ipstr[32];

  while (numOfRows < rows) {
    pShow->pNode = mpeerGetNextMnode(pShow->pNode, &pMnode);
    if (pMnode == NULL) break;

    cols = 0;

    pWrite = data + pShow->offset[cols] * rows + pShow->bytes[cols] * numOfRows;
    *(int16_t *)pWrite = pMnode->mnodeId;
    cols++;

    tinet_ntoa(ipstr, pMnode->privateIp);
    pWrite = data + pShow->offset[cols] * rows + pShow->bytes[cols] * numOfRows;
    strcpy(pWrite, ipstr);
    cols++;

    tinet_ntoa(ipstr, pMnode->publicIp);
    pWrite = data + pShow->offset[cols] * rows + pShow->bytes[cols] * numOfRows;
    strcpy(pWrite, ipstr);
    cols++;

    pWrite = data + pShow->offset[cols] * rows + pShow->bytes[cols] * numOfRows;
    *(int64_t *)pWrite = pMnode->createdTime;
    cols++;

    pWrite = data + pShow->offset[cols] * rows + pShow->bytes[cols] * numOfRows;
    strcpy(pWrite, mpeerGetMnodeRoleStr(pMnode->role));
    cols++;

    numOfRows++;

    mpeerReleaseMnode(pMnode);
  }

  pShow->numOfReads += numOfRows;

  return numOfRows;
}