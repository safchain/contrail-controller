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

    void EncodeNodeDelete(pugi::xml_node *parent,
                          const std::string &obj_typename,
                          const std::string &obj_name) {
        pugi::xml_node node = parent->append_child("node");
        node.append_attribute("type") = obj_typename.c_str();
        node.append_child("name").text().set(obj_name.c_str());
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

    void ConnectServiceTemplate(const std::string &si_name,
                                const std::string &tmpl_name) {
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
        EncodeNode(&update, "service-template", tmpl_name, &svc_template);

        update = config_.append_child("update");
        EncodeLink(&update,
                   "service-instance", si_name,
                   "service-template", tmpl_name,
                   "service-instance-service-template");
    }

    uuid ConnectVirtualMachine(const std::string &name) {
        boost::uuids::random_generator gen;
        autogen::IdPermsType id;
        id.Clear();
        uuid vm_id = gen();
        UuidTypeSet(vm_id, &id.uuid);

        autogen::VirtualMachine virtual_machine;
        virtual_machine.SetProperty("id-perms", &id);

        pugi::xml_node update = config_.append_child("update");
        EncodeNode(&update, "virtual-machine", name, &virtual_machine);

        update = config_.append_child("update");
        EncodeLink(&update,
                   "virtual-machine", name,
                   "service-instance", name,
                   "virtual-machine-service-instance");
        return vm_id;
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
        size_t loc = vmi_name.find(':');
        if (loc != std::string::npos) {
            vnet_name.append(vmi_name.substr(loc + 1));
        } else {
            vnet_name.append(vmi_name);
        }
        EncodeNode(&update, "virtual-network", vnet_name, &vnet);

        update = config_.append_child("update");
        EncodeLink(&update,
                   "virtual-machine-interface", vmi_name,
                   "virtual-network", vnet_name,
                   "virtual-machine-interface-virtual-network");
    }

    uuid ConnectVirtualMachineInterface(const std::string &vm_name,
                                        const std::string &vmi_name) {
        boost::uuids::random_generator gen;
        autogen::IdPermsType id;
        id.Clear();
        uuid vmi_id = gen();
        UuidTypeSet(vmi_id, &id.uuid);

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
        return vmi_id;
    }


  protected:
    std::auto_ptr<Agent> agent_;
    pugi::xml_document doc_;
    pugi::xml_node config_;
};

/*
 * Verifies that we can flatten the graph into the operational structure
 * properties that contains the elements necessary to start the netns.
 */
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
    ConnectServiceTemplate("test-1", "tmpl-1");
    parser->ConfigParse(config_, 1);
    task_util::WaitForIdle();

    MessageInit();
    uuid vm_id = ConnectVirtualMachine("test-1");
    uuid vmi1 = ConnectVirtualMachineInterface("test-1", "left");
    uuid vmi2 = ConnectVirtualMachineInterface("test-1", "right");
    parser->ConfigParse(config_, 1);
    task_util::WaitForIdle();

    EXPECT_EQ(vm_id, svc_instance->properties().instance_id);
    EXPECT_EQ(vmi1, svc_instance->properties().vmi_inside);
    EXPECT_EQ(vmi2, svc_instance->properties().vmi_outside);
}

/*
 * Ensure that the code can deal with multiple instances.
 * In this case, the same template is used for all instances.
 */
TEST_F(ServiceInstanceIntegrationTest, MultipleInstances) {
    static const int kNumTestInstances = 16;
    typedef std::vector<uuid> UuidList;
    UuidList svc_ids;
    UuidList vm_ids;
    UuidList vmi_inside_ids;
    UuidList vmi_outside_ids;

    for (int i = 0; i < kNumTestInstances; ++i) {
        boost::uuids::random_generator gen;
        uuid svc_id = gen();
        svc_ids.push_back(svc_id);

        MessageInit();
        std::stringstream name_gen;
        name_gen << "instance-" << i;
        EncodeServiceInstance(svc_id, name_gen.str());
        ConnectServiceTemplate(name_gen.str(), "nat-netns-template");

        vm_ids.push_back(
            ConnectVirtualMachine(name_gen.str()));
        std::string left_id = name_gen.str();
        left_id.append(":left");
        vmi_inside_ids.push_back(
            ConnectVirtualMachineInterface(name_gen.str(), left_id));

        std::string right_id = name_gen.str();
        right_id.append(":right");
        vmi_outside_ids.push_back(
            ConnectVirtualMachineInterface(name_gen.str(), right_id));

        IFMapAgentParser *parser = agent_->GetIfMapAgentParser();
        parser->ConfigParse(config_, 1);
    }

    task_util::WaitForIdle();

    for (int i = 0; i < kNumTestInstances; ++i) {
        ServiceInstanceTable *si_table = agent_->service_instance_table();

        ServiceInstanceKey key(svc_ids.at(i));
        ServiceInstance *svc_instance =
                static_cast<ServiceInstance *>(si_table->Find(&key, true));
        ASSERT_TRUE(svc_instance != NULL);

        EXPECT_EQ(vm_ids.at(i), svc_instance->properties().instance_id);
        EXPECT_EQ(vmi_inside_ids.at(i), svc_instance->properties().vmi_inside);
        EXPECT_EQ(vmi_outside_ids.at(i),
                  svc_instance->properties().vmi_outside);
    }
}

/*
 * Remove some of the links and ensure that the depedency tracking is
 * doing the right thing.
 */
TEST_F(ServiceInstanceIntegrationTest, RemoveLinks) {
    boost::uuids::random_generator gen;
    uuid svc_id = gen();
    EncodeServiceInstance(svc_id, "test-3");

    ConnectServiceTemplate("test-3", "tmpl-1");

    uuid vm_id = ConnectVirtualMachine("test-3");
    uuid vmi1 = ConnectVirtualMachineInterface("test-3", "left");
    uuid vmi2 = ConnectVirtualMachineInterface("test-3", "right");

    IFMapAgentParser *parser = agent_->GetIfMapAgentParser();
    parser->ConfigParse(config_, 1);
    task_util::WaitForIdle();

    ServiceInstanceTable *si_table = agent_->service_instance_table();
    ServiceInstanceKey key(svc_id);
    ServiceInstance *svc_instance =
            static_cast<ServiceInstance *>(si_table->Find(&key, true));
    ASSERT_TRUE(svc_instance != NULL);

    EXPECT_EQ(vm_id, svc_instance->properties().instance_id);
    EXPECT_EQ(vmi1, svc_instance->properties().vmi_inside);
    EXPECT_EQ(vmi2, svc_instance->properties().vmi_outside);

    /*
     * Remove the link between the vmi and the network.
     */
    MessageInit();
    pugi::xml_node msg = config_.append_child("delete");
    EncodeLink(&msg,
               "virtual-machine-interface", "left",
               "virtual-network", "vnet-left",
               "virtual-machine-interface-virtual-network");
    parser->ConfigParse(config_, 1);
    task_util::WaitForIdle();

    EXPECT_TRUE(svc_instance->properties().vmi_inside.is_nil());
    EXPECT_EQ(vmi2, svc_instance->properties().vmi_outside);

    /*
     * Removethe link between the instance and the virtual-machine object.
     */
    MessageInit();
    msg = config_.append_child("delete");
    EncodeLink(&msg,
               "virtual-machine", "test-3",
               "service-instance", "test-3",
               "virtual-machine-service-instance");
    parser->ConfigParse(config_, 1);
    task_util::WaitForIdle();

    EXPECT_TRUE(svc_instance->properties().instance_id.is_nil());
    EXPECT_TRUE(svc_instance->properties().vmi_inside.is_nil());
    EXPECT_TRUE(svc_instance->properties().vmi_outside.is_nil());
}

/*
 * Delete the service-instance object.
 */
TEST_F(ServiceInstanceIntegrationTest, Delete) {
    boost::uuids::random_generator gen;
    uuid svc_id = gen();
    EncodeServiceInstance(svc_id, "test-4");

    ConnectServiceTemplate("test-4", "tmpl-1");

    uuid vm_id = ConnectVirtualMachine("test-4");
    uuid vmi1 = ConnectVirtualMachineInterface("test-4", "left");
    uuid vmi2 = ConnectVirtualMachineInterface("test-4", "right");

    IFMapAgentParser *parser = agent_->GetIfMapAgentParser();
    parser->ConfigParse(config_, 1);
    task_util::WaitForIdle();

    ServiceInstanceTable *si_table = agent_->service_instance_table();
    ServiceInstanceKey key(svc_id);
    ServiceInstance *svc_instance =
            static_cast<ServiceInstance *>(si_table->Find(&key, true));
    ASSERT_TRUE(svc_instance != NULL);

    EXPECT_EQ(vm_id, svc_instance->properties().instance_id);
    EXPECT_EQ(vmi1, svc_instance->properties().vmi_inside);
    EXPECT_EQ(vmi2, svc_instance->properties().vmi_outside);

    pugi::xml_node msg = config_.append_child("delete");
    EncodeLink(&msg,
               "virtual-machine", "test-4",
               "service-instance", "test-4",
               "virtual-machine-service-instance");
    EncodeLink(&msg,
               "service-instance", "test-4",
               "service-template", "tmpl-1",
               "service-instance-service-template");
    EncodeNodeDelete(&msg, "service-instance", "test-4");
    parser->ConfigParse(config_, 1);
    task_util::WaitForIdle();

    DBEntry *entry = si_table->Find(&key, true);
    EXPECT_TRUE(entry == NULL);
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
