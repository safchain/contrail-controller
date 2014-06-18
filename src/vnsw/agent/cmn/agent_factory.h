/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_factory_hpp
#define vnsw_agent_factory_hpp

#include <boost/function.hpp>
#include "base/factory.h"

class Agent;
class AgentSignal;
class AgentUve;
class DB;
class DBGraph;
class EventManager;
class IFMapDependencyManager;
class KSync;
class NamespaceManager;
class AgentSignal;

class AgentObjectFactory : public Factory<AgentObjectFactory> {
    FACTORY_TYPE_N1(AgentObjectFactory, KSync, Agent *);
    FACTORY_TYPE_N2(AgentObjectFactory, AgentUve, Agent *, uint64_t);
    FACTORY_TYPE_N2(AgentObjectFactory, IFMapDependencyManager, DB *,
                    DBGraph *);
    FACTORY_TYPE_N2(AgentObjectFactory, NamespaceManager,
                    EventManager *, AgentSignal *);
    FACTORY_TYPE_N1(AgentObjectFactory, AgentSignal, EventManager *);
};

#endif // vnsw_agent_factory_hpp

