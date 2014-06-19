/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "oper/operdb_init.h"

/*
 * TODO: includes should be sorted by alphabetic order
 */
#include <cmn/agent_cmn.h>
#include "cmn/agent_param.h"
#include <db/db.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>
#include <cfg/cfg_init.h>
#include "cmn/agent_factory.h"
#include <oper/route_common.h>
#include "oper/ifmap_dependency_manager.h"
#include <oper/interface_common.h>
#include "oper/namespace_manager.h"
#include <oper/nexthop.h>
#include <oper/vrf.h>
#include <oper/mpls.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include "oper/service_instance.h"
#include <oper/sg.h>
#include <oper/mirror_table.h>
#include <oper/vrf_assign.h>
#include <oper/vxlan.h>
#include <oper/multicast.h>
#include <oper/global_vrouter.h>
#include <oper/agent_route_encap.h>
#include <base/task_trigger.h>

SandeshTraceBufferPtr OperDBTraceBuf(SandeshTraceBufferCreate("Oper DB", 5000));

void OperDB::CreateDBTables(DB *db) {
    DB::RegisterFactory("db.interface.0", &InterfaceTable::CreateTable);
    DB::RegisterFactory("db.nexthop.0", &NextHopTable::CreateTable);
    DB::RegisterFactory("uc.route.0",
                        &Inet4UnicastAgentRouteTable::CreateTable);
    DB::RegisterFactory("mc.route.0",
                        &Inet4MulticastAgentRouteTable::CreateTable);
    DB::RegisterFactory("l2.route.0", &Layer2AgentRouteTable::CreateTable);
    DB::RegisterFactory("db.vrf.0", &VrfTable::CreateTable);
    DB::RegisterFactory("db.vn.0", &VnTable::CreateTable);
    DB::RegisterFactory("db.vm.0", &VmTable::CreateTable);
    DB::RegisterFactory("db.sg.0", &SgTable::CreateTable);
    DB::RegisterFactory("db.mpls.0", &MplsTable::CreateTable);
    DB::RegisterFactory("db.acl.0", &AclTable::CreateTable);
    DB::RegisterFactory("db.mirror_table.0", &MirrorTable::CreateTable);
    DB::RegisterFactory("db.vrf_assign.0", &VrfAssignTable::CreateTable);
    DB::RegisterFactory("db.vxlan.0", &VxLanTable::CreateTable);
    DB::RegisterFactory("db.service-instance.0",
                        &ServiceInstanceTable::CreateTable);

    InterfaceTable *intf_table;
    intf_table = static_cast<InterfaceTable *>(db->CreateTable("db.interface.0"));
    assert(intf_table);
    agent_->set_interface_table(intf_table);
    intf_table->Init(this);
    intf_table->set_agent(agent_);

    NextHopTable *nh_table;
    nh_table = static_cast<NextHopTable *>(db->CreateTable("db.nexthop.0"));
    assert(nh_table);
    agent_->set_nexthop_table(nh_table);
    nh_table->set_agent(agent_);

    VrfTable *vrf_table;
    vrf_table = static_cast<VrfTable *>(db->CreateTable("db.vrf.0"));
    assert(vrf_table);
    agent_->set_vrf_table(vrf_table);
    vrf_table->set_agent(agent_);

    VmTable *vm_table;
    vm_table = static_cast<VmTable *>(db->CreateTable("db.vm.0"));
    assert(vm_table);
    agent_->set_vm_table(vm_table);
    vm_table->set_agent(agent_);

    SgTable *sg_table;
    sg_table = static_cast<SgTable *>(db->CreateTable("db.sg.0"));
    assert(sg_table);
    agent_->set_sg_table(sg_table);
    sg_table->set_agent(agent_);

    VnTable *vn_table;
    vn_table = static_cast<VnTable *>(db->CreateTable("db.vn.0"));
    assert(vn_table);
    agent_->set_vn_table(vn_table);
    vn_table->set_agent(agent_);

    MplsTable *mpls_table;
    mpls_table = static_cast<MplsTable *>(db->CreateTable("db.mpls.0"));
    assert(mpls_table);
    agent_->set_mpls_table(mpls_table);
    mpls_table->set_agent(agent_);

    AclTable *acl_table;
    acl_table = static_cast<AclTable *>(db->CreateTable("db.acl.0"));
    assert(acl_table);
    agent_->set_acl_table(acl_table);
    acl_table->set_agent(agent_);

    MirrorTable *mirror_table;
    mirror_table = static_cast<MirrorTable *>
                   (db->CreateTable("db.mirror_table.0"));
    assert(mirror_table);
    agent_->set_mirror_table(mirror_table);
    mirror_table->set_agent(agent_);

    VrfAssignTable *vassign_table = static_cast<VrfAssignTable *>
                   (db->CreateTable("db.vrf_assign.0"));
    assert(vassign_table);
    agent_->set_vrf_assign_table(vassign_table);
    vassign_table->set_agent(agent_);

    DomainConfig *domain_config_table = ;
    agent_->set_domain_config_table(domain_config_.get());

    VxLanTable *vxlan_table;
    vxlan_table = static_cast<VxLanTable *>(db->CreateTable("db.vxlan.0"));
    assert(vxlan_table);
    agent_->set_vxlan_table(vxlan_table);
    vxlan_table->set_agent(agent_);

    ServiceInstanceTable *si_table =
            static_cast<ServiceInstanceTable *>(
                db->CreateTable("db.service-instance.0"));
    agent_->SetServiceInstanceTable(si_table);
    si_table->Initialize(agent_->cfg()->cfg_graph(), dependency_manager_.get());

    multicast_.reset(new MulticastHandler(agent_));
    global_vrouter_.reset(new GlobalVrouter(this));
}

void OperDB::Init() {
    dependency_manager_->Initialize();

    // Unit tests may not initialize the agent configuration parameters.
    std::string netns_cmd;
    if (agent_->params()) {
        netns_cmd = agent_->params()->si_netns_command();
    }
    namespace_manager_->Initialize(agent_->GetDB(), netns_cmd);
}

void OperDB::RegisterDBClients() {
    multicast_.get()->Register();
    global_vrouter_.get()->CreateDBClients();
}

OperDB::OperDB(Agent *agent)
        : agent_(agent),
          dependency_manager_(
              AgentObjectFactory::Create<IFMapDependencyManager>(
                  agent->GetDB(), agent->cfg()->cfg_graph())),
          namespace_manager_(
                  AgentObjectFactory::Create<NamespaceManager>(
                      agent->GetEventManager(), agent->agent_signal())),
          domain_config_(new DomainConfig()) {
}

OperDB::~OperDB() {
}

void OperDB::Shutdown() {
    namespace_manager_->Terminate();
    dependency_manager_->Terminate();
    multicast_->Terminate();
    global_vrouter_.reset();

    agent_->acl_table()->Clear();
    agent_->interface_table()->Clear();
    agent_->mirror_table()->Clear();
    agent_->mpls_table()->Clear();
    agent_->nexthop_table()->Clear();
    agent_->service_instance_table()->Clear();
    agent_->sg_table()->Clear();
    agent_->vm_table()->Clear();
    agent_->vn_table()->Clear();
    agent_->vrf_assign_table()->Clear();
    agent_->vrf_table()->Clear();
    agent_->vxlan_table()->Clear();
}

void OperDB::DeleteRoutes() {
    agent_->vrf_table()->DeleteRoutes();
}
