/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_OPERDB_INIT__
#define __VNSW_OPERDB_INIT__

#include "base/util.h"

class Agent;
class DB;
class DomainConfig;
class GlobalVrouter;
class IFMapDependencyManager;
class MulticastHandler;
class NamespaceManager;
class PathPreferenceModule;

class OperDB {
public:
    OperDB(Agent *agent);
    virtual ~OperDB();

    void CreateDBTables(DB *);
    void RegisterDBClients();
    void Init();
    void CreateDefaultVrf();
    void DeleteRoutes();
    void Shutdown();

    Agent *agent() const { return agent_; }
    MulticastHandler *multicast() const { return multicast_.get(); }
    GlobalVrouter *global_vrouter() const { return global_vrouter_.get(); }
    PathPreferenceModule *route_preference_module() const {
        return route_preference_module_.get();
    }

    IFMapDependencyManager *dependency_manager() {
        return dependency_manager_.get();
    }

    NamespaceManager *namespace_manager() {
        return namespace_manager_.get();
    }

private:
    OperDB();

    Agent *agent_;
    std::auto_ptr<MulticastHandler> multicast_;
    std::auto_ptr<GlobalVrouter> global_vrouter_;
    std::auto_ptr<IFMapDependencyManager> dependency_manager_;
    std::auto_ptr<NamespaceManager> namespace_manager_;
    std::auto_ptr<DomainConfig> domain_config_;
    std::auto_ptr<PathPreferenceModule> route_preference_module_;

    DISALLOW_COPY_AND_ASSIGN(OperDB);
};
#endif
