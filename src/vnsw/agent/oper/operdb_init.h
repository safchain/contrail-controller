/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_OPERDB_INIT__
#define __VNSW_OPERDB_INIT__

#include "base/util.h"

class Agent;
class DB;
class GlobalVrouter;
class IFMapDependencyManager;
class MulticastHandler;

class OperDB {
public:
    OperDB(Agent *agent);
    virtual ~OperDB();

    void CreateDBTables(DB *);
    void CreateDBClients();
    void Init();
    void CreateDefaultVrf();
    void Shutdown();

    Agent *agent() const { return agent_; }
    MulticastHandler *multicast() const { return multicast_.get(); }
    GlobalVrouter *global_vrouter() const { return global_vrouter_.get(); }

    IFMapDependencyManager *dependency_manager() {
        return dependency_manager_.get();
    }

private:
    OperDB();
    static OperDB *singleton_;

    Agent *agent_;
    std::auto_ptr<MulticastHandler> multicast_;
    std::auto_ptr<GlobalVrouter> global_vrouter_;
    std::auto_ptr<IFMapDependencyManager> dependency_manager_;

    DISALLOW_COPY_AND_ASSIGN(OperDB);
};
#endif
