/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "oper/namespace_manager.h"

#include <boost/uuid/random_generator.hpp>

#include "base/test/task_test_util.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_agent_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/test/ifmap_test_util.h"
#include "oper/ifmap_dependency_manager.h"
#include "oper/service_instance.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

static boost::uuids::uuid IdPermsGetUuid(const autogen::IdPermsType &id) {
    boost::uuids::uuid uuid;
    CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
    return uuid;
}

class NamespaceManagerTest : public ::testing::Test {
  protected:
    NamespaceManagerTest() :
            dependency_manager_(
                new IFMapDependencyManager(&database_, &graph_)),
            ns_manager_(new NamespaceManager(&evm_, NULL)),
            si_table_(NULL) {
    }

    virtual void SetUp() {
        IFMapAgentLinkTable_Init(&database_, &graph_);
        vnc_cfg_Agent_ModuleInit(&database_, &graph_);
        dependency_manager_->Initialize();
        DB::RegisterFactory("db.service-instance.0",
                            &ServiceInstanceTable::CreateTable);
        si_table_ = static_cast<ServiceInstanceTable *>(
            database_.CreateTable("db.service-instance.0"));
        si_table_->Initialize(&graph_, dependency_manager_.get());
    }

    virtual void TearDown() {
        ns_manager_->Terminate();

        dependency_manager_->Terminate();
        IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
            database_.FindTable(IFMAP_AGENT_LINK_DB_NAME));
        assert(link_table);
        link_table->Clear();

        db_util::Clear(&database_);
    }

    boost::uuids::uuid AddServiceInstance(const std::string &name) {
        ifmap_test_util::IFMapMsgNodeAdd(&database_, "service-instance", name);
        task_util::WaitForIdle();
        IFMapTable *table = IFMapTable::FindTable(&database_,
                                                  "service-instance");
        IFMapNode *node = table->FindNode(name);
        if (node == NULL) {
            return boost::uuids::nil_uuid();
        }
        DBRequest request;
        si_table_->IFNodeToReq(node, request);
        si_table_->Enqueue(&request);
        task_util::WaitForIdle();

        autogen::ServiceInstance *si_object =
                static_cast<autogen::ServiceInstance *>(node->GetObject());
        const autogen::IdPermsType &id = si_object->id_perms();
        boost::uuids::uuid instance_id = IdPermsGetUuid(id);
        ServiceInstanceKey key(instance_id);
        ServiceInstance *svc_instance =
                static_cast<ServiceInstance *>(si_table_->Find(&key, true));
        if (svc_instance == NULL) {
            return boost::uuids::nil_uuid();
        }

        /*
         * Set non-nil uuids
         */
        ServiceInstance::Properties prop;
        prop.Clear();
        prop.virtualization_type = ServiceInstance::NetworkNamespace;
        boost::uuids::random_generator gen;
        prop.instance_id = gen();
        prop.vmi_inside = gen();
        prop.vmi_outside = gen();
        prop.ip_addr_inside = "10.0.0.1";
        prop.ip_addr_outside = "10.0.0.2";
        prop.ip_prefix_len_inside = 24;
        prop.ip_prefix_len_outside = 24;
        svc_instance->set_properties(prop);
        EXPECT_TRUE(svc_instance->IsUsable());
        si_table_->Change(svc_instance);
        return instance_id;
    }

    NamespaceState *ServiceInstanceState(boost::uuids::uuid id) {
        ServiceInstanceKey key(id);
        ServiceInstance *svc_instance =
                static_cast<ServiceInstance *>(si_table_->Find(&key, true));
        if (svc_instance == NULL) {
            return NULL;
        }
        return ns_manager_->GetState(svc_instance);
    }

    DB database_;
    DBGraph graph_;
    EventManager evm_;
    std::auto_ptr<IFMapDependencyManager> dependency_manager_;
    std::auto_ptr<NamespaceManager> ns_manager_;
    ServiceInstanceTable *si_table_;
};

TEST_F(NamespaceManagerTest, ExecTrue) {
    ns_manager_->Initialize(&database_, "/bin/true");
    boost::uuids::uuid id = AddServiceInstance("exec-true");
    EXPECT_FALSE(id.is_nil());
    task_util::WaitForIdle();
    NamespaceState *ns_state = ServiceInstanceState(id);
    ASSERT_TRUE(ns_state != NULL);
    EXPECT_EQ(0, ns_state->status());
}

static void SetUp() {
}

static void TearDown() {
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
