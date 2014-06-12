/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent_factory.h"

template <>
AgentObjectFactory *Factory<AgentObjectFactory>::singleton_ = NULL;

#include "oper/ifmap_dependency_manager.h"
#include "oper/namespace_manager.h"

FACTORY_STATIC_REGISTER(AgentObjectFactory, IFMapDependencyManager,
                        IFMapDependencyManager);
FACTORY_STATIC_REGISTER(AgentObjectFactory, NamespaceManager,
                        NamespaceManager);
