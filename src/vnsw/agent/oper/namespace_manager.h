/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_NAMESPACE_MANAGER_H__
#define __AGENT_OPER_NAMESPACE_MANAGER_H__

#include "db/db_table.h"

class Agent;
class ServiceInstance;

/*
 * Starts and stops network namespaces corresponding to service-instances.
 */
class NamespaceManager {
public:
    NamespaceManager(Agent *agent);

    void Initialize();
    void Terminate();

private:
    void StartNetworkNamespace(const ServiceInstance *svc_instance,
                               bool restart);
    void StopNetworkNamespace(const ServiceInstance *svc_instance);

    /*
     * Event observer for changes in the "db.service-instance.0" table.
     */
    void EventObserver(DBTablePartBase *db_part, DBEntryBase *entry);

    Agent *agent_;
    DBTableBase *si_table_;
    DBTableBase::ListenerId listener_id_;
};

#endif
