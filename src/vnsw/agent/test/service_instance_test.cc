/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "service_instance.h"

#include <boost/uuid/random_generator.hpp>
#include <pugixml/pugixml.hpp>
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

#include "base/test/task_test_util.h"
#include "cfg/cfg_init.h"
#include "oper/operdb_init.h"

using boost::uuids::uuid;

static std::string uuid_to_string(const uuid &uuid) {
    std::stringstream sgen;
    sgen << uuid;
    return sgen.str();
}

static void UuidTypeSet(const uuid &uuid, autogen::UuidType *idpair) {
    idpair->uuid_lslong = 0;
    idpair->uuid_mslong = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t value = uuid.data[16 - (i + 1)];
        idpair->uuid_lslong |= value << (8 * i);
    }
    for (int i = 0; i < 8; i++) {
        uint64_t value = uuid.data[8 - (i + 1)];
        idpair->uuid_mslong |= value << (8 * i);
    }
}

class ServiceInstanceIntegrationTest : public ::testing::Test {
  protected:
    ServiceInstanceIntegrationTest() {
        agent_.reset(new Agent);
    }

    void SetUp() {
        agent_->set_cfg(new AgentConfig(agent_.get()));
        agent_->set_oper_db(new OperDB(agent_.get()));
        agent_->CreateDBTables();
        agent_->CreateDBClients();
        agent_->InitModules();
        MessageInit();
    }

    void MessageInit() {
        doc_.reset();
        config_ = doc_.append_child("config");
    }

    void TearDown() {
        agent_->oper_db()->Shutdown();
    }

    void EncodeNode(pugi::xml_node *parent, const std::string &obj_typename,
                    const std::string &obj_name, const IFMapObject *object) {
        pugi::xml_node node = parent->append_child("node");
        node.append_attribute("type") = obj_typename.c_str();
        node.append_child("name").text().set(obj_name.c_str());
        object->EncodeUpdate(&node);
    }

    void EncodeLink(pugi::xml_node *parent,
                    const std::string &lhs_type,
                    const std::string &lhs_name,
                    const std::string &rhs_type,
                    const std::string &rhs_name,
                    const std::string &metadata) {
        pugi::xml_node link = parent->append_child("link");
        pugi::xml_node left = link.append_child("node");
        left.append_attribute("type") = lhs_type.c_str();
        left.append_child("name").text().set(lhs_name.c_str());
        pugi::xml_node right = link.append_child("node");
        right.append_attribute("type") = rhs_type.c_str();
        right.append_child("name").text().set(rhs_name.c_str());
        pugi::xml_node meta = link.append_child("metadata");
        meta.append_attribute("type") = metadata.c_str();
    }

    void EncodeServiceInstance(const uuid &uuid, const std::string &name) {
        autogen::IdPermsType id;
        id.Clear();
        UuidTypeSet(uuid, &id.uuid);
        autogen::ServiceInstance svc_instance;
        svc_instance.SetProperty("id-perms", &id);

        autogen::ServiceInstanceType svc_properties;
        svc_properties.Clear();
        svc_properties.left_virtual_network = "vnet-left";
        svc_properties.right_virtual_network = "vnet-right";
        svc_instance.SetProperty("service-instance-properties",
                                 &svc_properties);

        pugi::xml_node update = config_.append_child("update");
        EncodeNode(&update, "service-instance", name, &svc_instance);
    }

    void ConnectServiceTemplate(const std::string &name) {
        boost::uuids::random_generator gen;
        autogen::IdPermsType id;
        id.Clear();
        UuidTypeSet(gen(), &id.uuid);

        autogen::ServiceTemplate svc_template;
        svc_template.SetProperty("id-perms", &id);

        autogen::ServiceTemplateType props;
        // TODO: set service-type and instance-type
        props.Clear();

        pugi::xml_node update = config_.append_child("update");
        EncodeNode(&update, "service-template", name, &svc_template);

        update = config_.append_child("update");
        EncodeLink(&update,
                   "service-instance", name,
                   "service-template", name,
                   "service-instance-service-template");
    }

    void ConnectVirtualMachine(const std::string &name) {
        boost::uuids::random_generator gen;
        autogen::IdPermsType id;
        id.Clear();
        UuidTypeSet(gen(), &id.uuid);

        autogen::VirtualMachine virtual_machine;
        virtual_machine.SetProperty("id-perms", &id);

        pugi::xml_node update = config_.append_child("update");
        EncodeNode(&update, "virtual-machine", name, &virtual_machine);

        update = config_.append_child("update");
        EncodeLink(&update,
                   "virtual-machine", name,
                   "service-instance", name,
                   "virtual-machine-service-instance");
    }

    void ConnectVirtualNetwork(const std::string &vmi_name) {
        boost::uuids::random_generator gen;
        autogen::IdPermsType id;
        id.Clear();
        UuidTypeSet(gen(), &id.uuid);

        autogen::VirtualNetwork vnet;
        vnet.SetProperty("id-perms", &id);

        pugi::xml_node update = config_.append_child("update");
        std::string vnet_name("vnet-");
        vnet_name.append(vmi_name);
        EncodeNode(&update, "virtual-network", vnet_name, &vnet);

        update = config_.append_child("update");
        EncodeLink(&update,
                   "virtual-machine-interface", vmi_name,
                   "virtual-network", vnet_name,
                   "virtual-machine-interface-virtual-network");
    }

    void ConnectVirtualMachineInterface(const std::string &vm_name,
                                        const std::string &vmi_name) {
        boost::uuids::random_generator gen;
        autogen::IdPermsType id;
        id.Clear();
        UuidTypeSet(gen(), &id.uuid);

        autogen::VirtualMachineInterface vmi;
        vmi.SetProperty("id-perms", &id);

        pugi::xml_node update = config_.append_child("update");
        EncodeNode(&update, "virtual-machine-interface", vmi_name, &vmi);

        ConnectVirtualNetwork(vmi_name);

        update = config_.append_child("update");
        EncodeLink(&update,
                   "virtual-machine-interface", vmi_name,
                   "virtual-machine", vm_name,
                   "virtual-machine-interface-virtual-machine");
    }


  protected:
    std::auto_ptr<Agent> agent_;
    pugi::xml_document doc_;
    pugi::xml_node config_;
};

TEST_F(ServiceInstanceIntegrationTest, Config) {
    boost::uuids::random_generator gen;
    uuid svc_id = gen();
    EncodeServiceInstance(svc_id, "test-1");
    IFMapAgentParser *parser = agent_->GetIfMapAgentParser();
    parser->ConfigParse(config_, 1);
    task_util::WaitForIdle();

    ServiceInstanceTable *si_table = agent_->service_instance_table();
    EXPECT_EQ(1, si_table->Size());

    ServiceInstanceKey key(svc_id);
    ServiceInstance *svc_instance =
            static_cast<ServiceInstance *>(si_table->Find(&key, true));
    ASSERT_TRUE(svc_instance != NULL);

    MessageInit();
    ConnectServiceTemplate("test-1");
    parser->ConfigParse(config_, 1);
    task_util::WaitForIdle();

    MessageInit();
    ConnectVirtualMachine("test-1");
    ConnectVirtualMachineInterface("test-1", "left");
    ConnectVirtualMachineInterface("test-1", "right");
    parser->ConfigParse(config_, 1);
    task_util::WaitForIdle();

    EXPECT_FALSE(svc_instance->properties().instance_id.is_nil());
    EXPECT_FALSE(svc_instance->properties().vmi_inside.is_nil());
    EXPECT_FALSE(svc_instance->properties().vmi_outside.is_nil());
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
